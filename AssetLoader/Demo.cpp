/**
 * @file Demo.cpp
 * @brief Walk-through of the archive system: save several assets in one file, load them back by ID,
 *        show that damaged archives are detected, and load / migrate the previous format version.
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
#include "Commands.h"

namespace app
{
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

			std::cout << std::format("  {:<42} -> {}{}\n", step, asset::ToString(result.GetError()),
				asExpected ? "" : std::format("   [UNEXPECTED, wanted: {}]", asset::ToString(expected)));
		}

		/** Returns a copy of `archive` with its version field (bytes 4-5) replaced. */
		std::vector<std::byte> WithVersion(std::vector<std::byte> archive, std::uint16_t version)
		{
			archive[4] = static_cast<std::byte>(version & 0xFF);
			archive[5] = static_cast<std::byte>(version >> 8);
			return archive;
		}
	}

	int RunDemo(const std::filesystem::path& archivePath)
	{
		g_failures = 0;

		// Assets are identified by a 64-bit hash of their name.
		const asset::AssetId introId = asset::MakeAssetId("text/intro.txt");
		const asset::AssetId textureId = asset::MakeAssetId("textures/gradient.rgba");
		const asset::AssetId configId = asset::MakeAssetId("config/settings.ini");

		// 1. Save several assets in one archive file.
		PrintTitle("1. Writing the archive");
		asset::ArchiveWriter writer;
		AddSampleAssets(writer);
		std::cout << "  Added text/intro.txt, textures/gradient.rgba, config/settings.ini\n";
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

		// 3. Load assets by ID, without decoding the other assets.
		PrintTitle("3. Loading assets by ID");
		std::span<const std::byte> view;
		Report("Get text/intro.txt (no copy)", reader.GetAssetData(introId, view));
		std::cout << "    \"" << ToText(view) << "\"\n";

		std::vector<std::byte> texture;
		Report("Load textures/gradient.rgba (copy)", reader.LoadAsset(textureId, texture));
		if (texture.size() >= 4)
		{
			const auto channel = [&](std::size_t index) { return std::to_integer<int>(texture[index]); };
			const std::size_t last = texture.size() - 4;
			std::cout << std::format("    {} bytes, first pixel RGBA = ({}, {}, {}, {}), last pixel = ({}, {}, {}, {})\n",
				texture.size(), channel(0), channel(1), channel(2), channel(3),
				channel(last), channel(last + 1), channel(last + 2), channel(last + 3));
		}

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

		// 5. Versions: the previous version still loads and can be migrated; unknown versions are refused.
		PrintTitle("5. Format versions");
		asset::ArchiveWriter legacyWriter;
		Report("Choose format version 1 for a writer", legacyWriter.SetFormatVersion(1));
		AddSampleAssets(legacyWriter);

		asset::ArchiveReader legacy;
		Report("Open a version-1 archive", legacy.Open(legacyWriter.Build()));
		std::cout << std::format("    version {}, {} bytes (no checksums: added fields set to 0)\n",
			legacy.GetHeader().version, legacy.GetHeader().archiveSize);
		Report("Get text/intro.txt from version 1", legacy.GetAssetData(introId, view));
		std::cout << "    \"" << ToText(view) << "\"\n";

		asset::ArchiveWriter upgradeWriter;
		Report("Migrate it to the current version", upgradeWriter.AddAssets(legacy));
		asset::ArchiveReader upgraded;
		Report("Open the migrated archive", upgraded.Open(upgradeWriter.Build()));
		std::cout << std::format("    version {}, {} bytes, checksums verified: {}\n",
			upgraded.GetHeader().version, upgraded.GetHeader().archiveSize, upgraded.HasChecksums() ? "yes" : "no");

		Report("Archive of an unknown version (99)", checker.Open(WithVersion(original, 99)),
			asset::ArchiveError::UnsupportedVersion);
		Report("Write an unknown version (3)", legacyWriter.SetFormatVersion(3), asset::ArchiveError::UnsupportedVersion);

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
}
