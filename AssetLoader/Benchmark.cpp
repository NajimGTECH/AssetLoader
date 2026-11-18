/**
 * @file Benchmark.cpp
 * @brief Measures archive size, save / load times and temporary memory for two data volumes.
 *
 * Method:
 *  - The data is reproducible: each asset is filled by a std::mt19937 seeded with its index, and
 *    the lookup order is shuffled with a fixed seed. Every run produces the same archive.
 *  - Each operation runs kRuns times; the median time is reported (std::chrono::steady_clock).
 *  - Memory is measured by counting every operator new / delete of the executable (MemoryTracker):
 *    "peak" is the highest amount allocated at the same time during the operation, above what was
 *    allocated before it; "kept" is what is still allocated when it returns (owned by its result).
 */
#include <algorithm>
#include <chrono>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <random>
#include <span>
#include <string>
#include <utility>
#include <vector>

#include "Archive/ArchiveReader.h"
#include "Archive/ArchiveWriter.h"
#include "Commands.h"
#include "MemoryTracker.h"

namespace app
{
	namespace
	{
		struct Volume
		{
			const char* name;
			std::size_t assetCount;
			std::size_t assetSize;
		};

		constexpr Volume kVolumes[] = {
			{ "Small", 100, 1024 },         // 100 assets of 1 KiB   (100 KiB of data)
			{ "Large", 1000, 64 * 1024 },   // 1000 assets of 64 KiB (62.5 MiB of data)
		};

		constexpr int kRuns = 5;

		struct Measurement
		{
			double medianMs = 0.0;
			std::size_t peakBytes = 0;
			std::int64_t keptBytes = 0;
			std::uint64_t allocations = 0;
		};

		/** Set to false if an archive operation fails during the benchmark. */
		bool g_allOk = true;

		void Check(asset::ArchiveResult result)
		{
			if (!result)
			{
				g_allOk = false;
				std::cout << "  Operation failed: " << asset::ToString(result.GetError()) << '\n';
			}
		}

		std::string FormatBytes(double bytes)
		{
			if (bytes >= 1024.0 * 1024.0)
			{
				return std::format("{:.2f} MiB", bytes / (1024.0 * 1024.0));
			}
			if (bytes >= 1024.0)
			{
				return std::format("{:.1f} KiB", bytes / 1024.0);
			}
			return std::format("{} B", bytes);
		}

		/**
		 * Runs `operation` kRuns times. Its return value stays alive until the memory is measured,
		 * so what it owns (a reader, a buffer...) is counted as "kept", then it is destroyed.
		 */
		template <typename Operation>
		Measurement Measure(Operation&& operation)
		{
			Measurement measurement;
			std::vector<double> times;
			times.reserve(kRuns); // so that recording a time does not count as an allocation of the operation

			for (int run = 0; run < kRuns; ++run)
			{
				const memory::Scope scope;
				const auto start = std::chrono::steady_clock::now();
				auto result = operation();
				const auto end = std::chrono::steady_clock::now();

				times.push_back(std::chrono::duration<double, std::milli>(end - start).count());
				measurement.peakBytes = std::max(measurement.peakBytes, scope.GetPeakBytes());
				measurement.keptBytes = scope.GetRetainedBytes();
				measurement.allocations = scope.GetAllocationCount();
				static_cast<void>(result);
			}

			std::sort(times.begin(), times.end());
			measurement.medianMs = times[times.size() / 2];
			return measurement;
		}

		void PrintTableHeader()
		{
			std::cout << std::format("| {:<38} | {:>13} | {:>11} | {:>11} | {:>6} |\n",
				"Operation", "Median time", "Peak memory", "Kept after", "Allocs");
			std::cout << std::format("|{:-<40}|{:->14}:|{:->12}:|{:->12}:|{:->7}:|\n", "", "", "", "", "");
		}

		void PrintRow(const char* operation, const Measurement& measurement)
		{
			std::cout << std::format("| {:<38} | {:>10.3f} ms | {:>11} | {:>11} | {:>6} |\n",
				operation, measurement.medianMs, FormatBytes(static_cast<double>(measurement.peakBytes)),
				FormatBytes(static_cast<double>(std::max<std::int64_t>(measurement.keptBytes, 0))),
				measurement.allocations);
		}

		struct SourceAsset
		{
			asset::AssetId id;
			std::vector<std::byte> data;
		};

		std::vector<SourceAsset> MakeAssets(const Volume& volume)
		{
			std::vector<SourceAsset> assets;
			assets.reserve(volume.assetCount);
			for (std::size_t i = 0; i < volume.assetCount; ++i)
			{
				std::mt19937 generator(static_cast<std::uint32_t>(i));
				std::vector<std::byte> data(volume.assetSize);
				for (std::byte& byte : data)
				{
					byte = static_cast<std::byte>(generator() & 0xFF);
				}
				assets.push_back({ asset::MakeAssetId(std::format("bench/asset_{:05}.bin", i)), std::move(data) });
			}
			return assets;
		}

		void RunVolume(const Volume& volume, const std::filesystem::path& path)
		{
			const std::vector<SourceAsset> assets = MakeAssets(volume);

			std::vector<asset::AssetId> lookupOrder;
			for (const SourceAsset& source : assets)
			{
				lookupOrder.push_back(source.id);
			}
			std::shuffle(lookupOrder.begin(), lookupOrder.end(), std::mt19937(2026));

			const auto addAll = [&]()
			{
				asset::ArchiveWriter writer;
				for (const SourceAsset& source : assets)
				{
					Check(writer.AddAsset(source.id, asset::AssetType::Binary, source.data));
				}
				return writer;
			};

			// Reference objects for the operations that need an existing writer / archive.
			const asset::ArchiveWriter writer = addAll();
			const std::vector<std::byte> archive = writer.Build();
			Check(writer.SaveToFile(path));
			asset::ArchiveReader openReader;
			Check(openReader.OpenFile(path));

			const std::size_t payload = volume.assetCount * volume.assetSize;
			std::cout << std::format("\n### {}: {} assets x {}\n\n", volume.name, volume.assetCount,
				FormatBytes(static_cast<double>(volume.assetSize)));
			std::cout << std::format("Archive size: {} bytes ({}), of which {} bytes of header and directory ({:.3f} %).\n\n",
				archive.size(), FormatBytes(static_cast<double>(archive.size())), archive.size() - payload,
				100.0 * static_cast<double>(archive.size() - payload) / static_cast<double>(archive.size()));

			PrintTableHeader();

			PrintRow("Add assets to the writer (copy)", Measure(addAll));

			PrintRow("Build the archive in memory", Measure([&]() { return writer.Build(); }));

			PrintRow("Save to file (build + write)", Measure([&]()
			{
				const asset::ArchiveResult result = writer.SaveToFile(path);
				Check(result);
				return result;
			}));

			PrintRow("Open from memory (checks + CRC-32)", Measure([&]()
			{
				asset::ArchiveReader reader;
				Check(reader.Open(std::span<const std::byte>(archive)));
				return reader;
			}));

			PrintRow("Open file (read + checks + CRC-32)", Measure([&]()
			{
				asset::ArchiveReader reader;
				Check(reader.OpenFile(path));
				return reader;
			}));

			PrintRow("Get every asset by ID (zero-copy view)", Measure([&]()
			{
				std::size_t total = 0;
				for (const asset::AssetId id : lookupOrder)
				{
					std::span<const std::byte> view;
					Check(openReader.GetAssetData(id, view));
					total += view.size();
				}
				return total;
			}));

			PrintRow("Load every asset by ID (copy)", Measure([&]()
			{
				std::size_t total = 0;
				for (const asset::AssetId id : lookupOrder)
				{
					std::vector<std::byte> copy;
					Check(openReader.LoadAsset(id, copy));
					total += copy.size();
				}
				return total;
			}));
		}
	}

	int RunBenchmark()
	{
#ifdef _DEBUG
		constexpr const char* kConfiguration = "Debug (times are not representative, use Release)";
#else
		constexpr const char* kConfiguration = "Release";
#endif
#ifdef _WIN64
		constexpr const char* kPlatform = "x64";
#else
		constexpr const char* kPlatform = "x86";
#endif

		g_allOk = true;
		const std::filesystem::path path = std::filesystem::temp_directory_path() / "AssetLoaderBenchmark.aset";

		std::cout << std::format("## Measurements ({} {}, median of {} runs)\n", kConfiguration, kPlatform, kRuns);
		for (const Volume& volume : kVolumes)
		{
			RunVolume(volume, path);
		}

		std::error_code ignored;
		std::filesystem::remove(path, ignored);

		if (!g_allOk)
		{
			std::cout << "\nSome operations failed: the measurements are not valid.\n";
			return 1;
		}
		return 0;
	}
}
