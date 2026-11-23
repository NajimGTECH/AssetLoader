#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "Archive/ArchiveReader.h"
#include "Archive/ArchiveWriter.h"
#include "Commands.h"

namespace app
{
	// --- Shared helpers ------------------------------------------------------------------------

	std::vector<std::byte> ToBytes(std::string_view text)
	{
		const auto bytes = std::as_bytes(std::span<const char>(text.data(), text.size()));
		return { bytes.begin(), bytes.end() };
	}

	std::string_view ToText(std::span<const std::byte> bytes)
	{
		return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
	}

	namespace
	{
		/** A small 4x4 RGBA image (a red to blue gradient), standing in for a real texture. */
		std::vector<std::byte> MakeTexture()
		{
			constexpr int kSide = 4;
			std::vector<std::byte> pixels;
			pixels.reserve(kSide * kSide * 4);
			for (int y = 0; y < kSide; ++y)
			{
				for (int x = 0; x < kSide; ++x)
				{
					const int blend = (x + y) * 255 / (2 * (kSide - 1));
					pixels.push_back(static_cast<std::byte>(255 - blend)); // R
					pixels.push_back(std::byte{ 0 });                       // G
					pixels.push_back(static_cast<std::byte>(blend));       // B
					pixels.push_back(std::byte{ 255 });                     // A
				}
			}
			return pixels;
		}

		/** Opens `path` and prints why it failed, if it did. */
		bool OpenOrReport(asset::ArchiveReader& reader, const std::filesystem::path& path)
		{
			if (const asset::ArchiveResult result = reader.OpenFile(path); !result)
			{
				std::cout << "Cannot open " << path.string() << ": " << asset::ToString(result.GetError()) << '\n';
				return false;
			}
			return true;
		}
	}

	void AddSampleAssets(asset::ArchiveWriter& writer)
	{
		// The IDs are new and valid, so these calls cannot fail.
		static_cast<void>(writer.AddAsset(asset::MakeAssetId("text/intro.txt"), asset::AssetType::Text,
			ToBytes("Welcome! This text was loaded from an asset archive.")));
		static_cast<void>(writer.AddAsset(asset::MakeAssetId("textures/gradient.rgba"), asset::AssetType::Texture,
			MakeTexture()));
		static_cast<void>(writer.AddAsset(asset::MakeAssetId("config/settings.ini"), asset::AssetType::Text,
			ToBytes("[window]\nwidth=1280\nheight=720\n")));
	}

	void PrintDirectory(const asset::ArchiveReader& reader)
	{
		const asset::ArchiveHeader& header = reader.GetHeader();
		std::cout << std::format("  Format version {}, {} assets, {} bytes, directory at offset {}\n",
			header.version, header.assetCount, header.archiveSize, header.directoryOffset);
		std::cout << "  Checksums: " << (reader.HasChecksums()
			? "header, directory and every asset verified"
			: "none in this version (structure checked, corruption cannot be detected)") << '\n';

		std::cout << std::format("  {:<18} {:<9} {:>7} {:>6}  {}\n", "ID", "Type", "Offset", "Size", "CRC-32");
		for (const asset::AssetEntry& entry : reader.GetEntries())
		{
			std::cout << std::format("  {:016X}   {:<9} {:>7} {:>6}  {}\n",
				entry.id, asset::ToString(entry.type), entry.offset, entry.size,
				reader.HasChecksums() ? std::format("{:08X}", entry.crc32) : std::string("-"));
		}
	}

	// --- Commands ----------------------------------------------------------------------------

	int RunSamples(const std::filesystem::path& directory)
	{
		std::error_code error;
		std::filesystem::create_directories(directory, error);
		if (error)
		{
			std::cout << "Cannot create " << directory.string() << ": " << error.message() << '\n';
			return 1;
		}

		// The same assets in every supported version, to show that both load to the same content.
		for (std::uint16_t version = asset::kOldestArchiveVersion; version <= asset::kArchiveVersion; ++version)
		{
			asset::ArchiveWriter writer;
			static_cast<void>(writer.SetFormatVersion(version)); // supported by construction of the loop
			AddSampleAssets(writer);

			const std::filesystem::path path = directory / std::format("sample_v{}.aset", version);
			if (const asset::ArchiveResult result = writer.SaveToFile(path); !result)
			{
				std::cout << "Cannot write " << path.string() << ": " << asset::ToString(result.GetError()) << '\n';
				return 1;
			}

			asset::ArchiveReader reader;
			if (!OpenOrReport(reader, path))
			{
				return 1;
			}
			std::cout << "Wrote " << path.string() << '\n';
			PrintDirectory(reader);
		}

		// Physics scenes in both scene versions, and the general-purpose types.
		return RunSceneSamples(directory);
	}

	int RunInspect(const std::filesystem::path& archivePath)
	{
		asset::ArchiveReader reader;
		if (!OpenOrReport(reader, archivePath))
		{
			return 1;
		}

		std::cout << archivePath.string() << '\n';
		PrintDirectory(reader);
		return 0;
	}

	int RunMigrate(const std::filesystem::path& source, const std::filesystem::path& destination)
	{
		asset::ArchiveReader oldArchive;
		if (!OpenOrReport(oldArchive, source))
		{
			return 1;
		}

		asset::ArchiveWriter writer; // writes the current version by default
		if (const asset::ArchiveResult result = writer.AddAssets(oldArchive); !result)
		{
			std::cout << "Cannot copy the assets: " << asset::ToString(result.GetError()) << '\n';
			return 1;
		}
		if (const asset::ArchiveResult result = writer.SaveToFile(destination); !result)
		{
			std::cout << "Cannot write " << destination.string() << ": " << asset::ToString(result.GetError()) << '\n';
			return 1;
		}

		asset::ArchiveReader upgraded;
		if (!OpenOrReport(upgraded, destination))
		{
			return 1;
		}
		std::cout << std::format("Migrated {} (version {}) to {} (version {})\n",
			source.string(), oldArchive.GetHeader().version, destination.string(), upgraded.GetHeader().version);
		PrintDirectory(upgraded);
		return 0;
	}
}
