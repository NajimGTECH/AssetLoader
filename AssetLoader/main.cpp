/**
 * @file main.cpp
 * @brief Demo of the archive system: save several assets in one file, load them back by ID,
 *        and show that damaged archives are detected.
 *
 * Usage: AssetLoader.exe [archive path]
 * Without an argument, the archive is written to the system temporary folder.
 */
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Archive/ArchiveReader.h"
#include "Archive/ArchiveWriter.h"

namespace
{
	/** Counts the steps whose result was not the expected one. */
	int g_failures = 0;

	void PrintTitle(std::string_view title)
	{
		std::cout << "\n=== " << title << " ===\n";
	}

	/** Prints the result of a step and remembers it when it is not the expected one. */
	void Report(std::string_view step, asset::ArchiveResult result, asset::ArchiveError expected = asset::ArchiveError::None)
	{
		const bool asExpected = result.GetError() == expected;
		if (!asExpected)
		{
			++g_failures;
		}

		std::cout << std::format("  {:<40} -> {}{}\n", step, asset::ToString(result.GetError()),
			asExpected ? "" : std::format("   [UNEXPECTED, wanted: {}]", asset::ToString(expected)));
	}

	std::vector<std::byte> ToBytes(std::string_view text)
	{
		const auto bytes = std::as_bytes(std::span<const char>(text.data(), text.size()));
		return { bytes.begin(), bytes.end() };
	}

	std::string_view ToText(std::span<const std::byte> bytes)
	{
		return { reinterpret_cast<const char*>(bytes.data()), bytes.size() };
	}

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

	void PrintDirectory(const asset::ArchiveReader& reader)
	{
		const asset::ArchiveHeader& header = reader.GetHeader();
		std::cout << std::format("  Format version {}, {} assets, {} bytes, directory at offset {}\n",
			header.version, header.assetCount, header.archiveSize, header.directoryOffset);

		std::cout << std::format("  {:<18} {:<9} {:>7} {:>6}  {}\n", "ID", "Type", "Offset", "Size", "CRC-32");
		for (const asset::AssetEntry& entry : reader.GetEntries())
		{
			std::cout << std::format("  {:016X}   {:<9} {:>7} {:>6}  {:08X}\n",
				entry.id, asset::ToString(entry.type), entry.offset, entry.size, entry.crc32);
		}
	}
}

int main(int argc, char* argv[])
{
	const std::filesystem::path archivePath = argc > 1
		? std::filesystem::path(argv[1])
		: std::filesystem::temp_directory_path() / "AssetLoaderDemo.aset";

	// Assets are identified by a 64-bit hash of their name.
	const asset::AssetId introId = asset::MakeAssetId("text/intro.txt");
	const asset::AssetId textureId = asset::MakeAssetId("textures/gradient.rgba");
	const asset::AssetId configId = asset::MakeAssetId("config/settings.ini");

	// 1. Save several assets in one archive file.
	PrintTitle("1. Writing the archive");
	asset::ArchiveWriter writer;
	Report("Add text/intro.txt", writer.AddAsset(introId, asset::AssetType::Text,
		ToBytes("Welcome! This text was loaded from an asset archive.")));
	Report("Add textures/gradient.rgba", writer.AddAsset(textureId, asset::AssetType::Texture, MakeTexture()));
	Report("Add config/settings.ini", writer.AddAsset(configId, asset::AssetType::Text,
		ToBytes("[window]\nwidth=1280\nheight=720\n")));
	Report("Add text/intro.txt again", writer.AddAsset(introId, asset::AssetType::Text, ToBytes("duplicate")),
		asset::ArchiveError::DuplicateAssetId);
	Report("Save " + archivePath.filename().string(), writer.SaveToFile(archivePath));
	std::cout << "  File: " << archivePath.string() << '\n';

	// 2. Open it: the header, the directory and every asset are checked before Open succeeds.
	PrintTitle("2. Reading the archive");
	asset::ArchiveReader reader;
	Report("Open the file", reader.OpenFile(archivePath));
	if (!reader.IsOpen())
	{
		std::cout << "\nThe archive could not be opened, stopping the demo.\n";
		return 1;
	}
	PrintDirectory(reader);

	// 3. Load assets by ID.
	PrintTitle("3. Loading assets by ID");
	std::span<const std::byte> view;
	Report("Get text/intro.txt (no copy)", reader.GetAssetData(introId, view));
	std::cout << "    \"" << ToText(view) << "\"\n";

	std::vector<std::byte> texture;
	Report("Load textures/gradient.rgba (copy)", reader.LoadAsset(textureId, texture));
	std::cout << std::format("    {} bytes, first pixel RGBA = ({}, {}, {}, {}), last pixel = ({}, {}, {}, {})\n",
		texture.size(),
		std::to_integer<int>(texture[0]), std::to_integer<int>(texture[1]),
		std::to_integer<int>(texture[2]), std::to_integer<int>(texture[3]),
		std::to_integer<int>(texture[texture.size() - 4]), std::to_integer<int>(texture[texture.size() - 3]),
		std::to_integer<int>(texture[texture.size() - 2]), std::to_integer<int>(texture[texture.size() - 1]));

	Report("Get config/settings.ini", reader.GetAssetData(configId, view));
	std::cout << "    " << view.size() << " bytes of text\n";

	Report("Get music/theme.ogg (not in the archive)",
		reader.GetAssetData(asset::MakeAssetId("music/theme.ogg"), view), asset::ArchiveError::AssetNotFound);

	// 4. Damaged archives are refused, with an error that says what is wrong.
	PrintTitle("4. Detecting damaged archives");
	const std::vector<std::byte> original = writer.Build();
	const std::size_t lastByte = original.size() - 1;
	asset::ArchiveReader checker;

	std::vector<std::byte> damaged = original;
	damaged[0] = std::byte{ 'X' };
	Report("Wrong magic number", checker.Open(std::move(damaged)), asset::ArchiveError::InvalidMagic);

	damaged = original;
	damaged[10] ^= std::byte{ 0x01 }; // one bit of the asset count
	Report("One bit flipped in the header", checker.Open(std::move(damaged)), asset::ArchiveError::HeaderChecksumMismatch);

	damaged = original;
	damaged[asset::kHeaderSize + 12] ^= std::byte{ 0x01 }; // one bit of the first entry's offset
	Report("One bit flipped in the directory", checker.Open(std::move(damaged)), asset::ArchiveError::DirectoryChecksumMismatch);

	damaged = original;
	damaged[lastByte] ^= std::byte{ 0x01 }; // one bit of the last asset
	Report("One bit flipped in an asset", checker.Open(std::move(damaged)), asset::ArchiveError::AssetChecksumMismatch);

	damaged = original;
	damaged.resize(damaged.size() - 10);
	Report("File cut short", checker.Open(std::move(damaged)), asset::ArchiveError::TruncatedArchive);

	damaged = original;
	damaged.push_back(std::byte{ 0 });
	Report("Extra byte at the end", checker.Open(std::move(damaged)), asset::ArchiveError::ArchiveSizeMismatch);

	Report("Missing file", checker.OpenFile(archivePath.parent_path() / "does-not-exist.aset"),
		asset::ArchiveError::FileOpenFailed);

	// Summary.
	PrintTitle("Done");
	if (g_failures == 0)
	{
		std::cout << "  Every step gave the expected result.\n";
		return 0;
	}
	std::cout << "  " << g_failures << " step(s) did not give the expected result.\n";
	return 1;
}
