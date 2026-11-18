#include "pch.h"
#include "TestHelpers.h"
#include "ArchiveTestHelpers.h"

#include "Archive/ArchiveFormat.h"
#include "Archive/ArchiveReader.h"
#include "Archive/ArchiveWriter.h"
#include "Integrity/Crc32.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace tests;

namespace VersioningTests
{
	namespace
	{
		/** Returns a copy of `archive` with the version field (bytes 4-5) replaced. */
		std::vector<std::byte> WithVersion(std::span<const std::byte> archive, std::uint16_t version)
		{
			std::vector<std::byte> copy(archive.begin(), archive.end());
			copy[4] = static_cast<std::byte>(version & 0xFF);
			copy[5] = static_cast<std::byte>(version >> 8);
			return copy;
		}

		/** Offset of the data section of the version-1 sample archive (3 entries). */
		constexpr std::size_t kVersion1DataOffset = 28 + 3 * 28;
	}

	/*
	* Format versions: the current reader loads version-1 archives (no checksums) with default
	* values for the fields added in version 2, migrates them to the current version, and rejects
	* every unknown version.
	*/
	TEST_CLASS(ArchiveVersioningTests)
	{
	public:

		// --- Writer ----------------------------------------------------------------------------

		TEST_METHOD(WriterUsesTheCurrentVersionByDefault)
		{
			ArchiveWriter writer;
			Assert::AreEqual(kArchiveVersion, writer.GetFormatVersion());
		}

		TEST_METHOD(SetFormatVersionRejectsUnsupportedVersions)
		{
			ArchiveWriter writer;
			AssertOk(writer.SetFormatVersion(1));

			for (const std::uint16_t version : { std::uint16_t{ 0 }, std::uint16_t{ kArchiveVersion + 1 }, std::uint16_t{ 0xFFFF } })
			{
				AssertFails(ArchiveError::UnsupportedVersion, writer.SetFormatVersion(version));
				Assert::AreEqual(std::uint16_t{ 1 }, writer.GetFormatVersion(), L"Writer modified on failure");
			}
		}

		TEST_METHOD(Version1LayoutHasNoChecksums)
		{
			const std::vector<std::byte> archive = BuildSampleArchive(1);

			// 28-byte header, three 28-byte entries, then 5 + 8 + 0 bytes of data.
			Assert::AreEqual(kVersion1DataOffset + 13, archive.size());
			Assert::AreEqual(std::byte{ 0x01 }, archive[4]);
			Assert::AreEqual(std::byte{ 0x00 }, archive[5]);
		}

		// --- Reading the previous version --------------------------------------------------------

		TEST_METHOD(ReaderLoadsVersion1Archive)
		{
			ArchiveReader reader;
			AssertOk(reader.Open(BuildSampleArchive(1)));

			AssertSampleContent(reader);
			Assert::AreEqual(std::uint16_t{ 1 }, reader.GetHeader().version);
			Assert::IsFalse(reader.HasChecksums());
		}

		TEST_METHOD(AddedFieldsGetTheirDefaultValue)
		{
			ArchiveReader reader;
			AssertOk(reader.Open(BuildSampleArchive(1)));

			Assert::AreEqual(std::uint32_t{ 0 }, reader.GetHeader().directoryCrc);
			for (const AssetEntry& entry : reader.GetEntries())
			{
				Assert::AreEqual(std::uint32_t{ 0 }, entry.crc32);
			}
		}

		TEST_METHOD(Version1AndVersion2HaveTheSameContent)
		{
			ArchiveReader version1;
			ArchiveReader version2;
			AssertOk(version1.Open(BuildSampleArchive(1)));
			AssertOk(version2.Open(BuildSampleArchive(2)));

			Assert::IsTrue(version2.HasChecksums());
			Assert::AreEqual(version1.GetAssetCount(), version2.GetAssetCount());
			for (std::size_t i = 0; i < version1.GetAssetCount(); ++i)
			{
				const AssetEntry& old = version1.GetEntries()[i];
				const AssetEntry& current = version2.GetEntries()[i];
				Assert::AreEqual(old.id, current.id);
				Assert::IsTrue(old.type == current.type);
				Assert::AreEqual(old.size, current.size);

				std::span<const std::byte> oldData;
				std::span<const std::byte> currentData;
				AssertOk(version1.GetAssetData(old.id, oldData));
				AssertOk(version2.GetAssetData(current.id, currentData));
				AssertBytesEqual(oldData, currentData);
			}
		}

		TEST_METHOD(Version1ArchiveIsStillValidated)
		{
			const std::vector<std::byte> archive = BuildSampleArchive(1);
			ArchiveReader reader;

			// Truncation and extra bytes: archiveSize already existed in version 1.
			std::vector<std::byte> truncated = archive;
			truncated.pop_back();
			AssertFails(ArchiveError::TruncatedArchive, reader.Open(std::move(truncated)));

			std::vector<std::byte> extended = archive;
			extended.push_back(std::byte{ 0 });
			AssertFails(ArchiveError::ArchiveSizeMismatch, reader.Open(std::move(extended)));

			// Metadata: the first entry's offset (bytes 12-19 of the entry) pointing past the end.
			std::vector<std::byte> outOfBounds = archive;
			outOfBounds[28 + 12 + 7] = std::byte{ 0x7F };
			AssertFails(ArchiveError::AssetOutOfBounds, reader.Open(std::move(outOfBounds)));

			// Unknown type in the first entry (bytes 8-11).
			std::vector<std::byte> badType = archive;
			badType[28 + 8] = std::byte{ 0xEE };
			AssertFails(ArchiveError::InvalidAssetType, reader.Open(std::move(badType)));

			// Directory placed inside the 28-byte header.
			std::vector<std::byte> badDirectory = archive;
			badDirectory[12] = std::byte{ 27 };
			AssertFails(ArchiveError::InvalidDirectory, reader.Open(std::move(badDirectory)));

			Assert::IsFalse(reader.IsOpen());
		}

		TEST_METHOD(Version1CannotDetectCorruptedData)
		{
			// Known limitation of version 1, the reason version 2 exists: without checksums, a
			// damaged asset byte is not detected. HasChecksums() lets the caller know.
			std::vector<std::byte> damaged = BuildSampleArchive(1);
			damaged[kVersion1DataOffset] ^= std::byte{ 0x01 };

			ArchiveReader reader;
			AssertOk(reader.Open(std::move(damaged)));
			Assert::IsFalse(reader.HasChecksums());

			// The same damage in a version-2 archive is detected.
			const std::vector<std::byte> current = BuildSampleArchive(2);
			AssertFails(ArchiveError::AssetChecksumMismatch,
				reader.Open(Corrupt(current, static_cast<std::size_t>(DataOffset(3)))));
		}

		// --- Migration ---------------------------------------------------------------------------

		TEST_METHOD(MigrationToTheCurrentVersion)
		{
			ArchiveReader oldArchive;
			AssertOk(oldArchive.Open(BuildSampleArchive(1)));

			ArchiveWriter writer;
			AssertOk(writer.AddAssets(oldArchive));
			Assert::AreEqual(std::size_t{ 3 }, writer.GetAssetCount());

			ArchiveReader upgraded;
			AssertOk(upgraded.Open(writer.Build()));
			AssertSampleContent(upgraded);
			Assert::AreEqual(kArchiveVersion, upgraded.GetHeader().version);
			Assert::IsTrue(upgraded.HasChecksums());

			// The checksums were computed during the migration.
			for (const AssetEntry& entry : upgraded.GetEntries())
			{
				std::span<const std::byte> data;
				AssertOk(upgraded.GetAssetData(entry.id, data));
				Assert::AreEqual(ComputeCrc32(data), entry.crc32);
			}

			// Same bytes as an archive written directly in the current version.
			AssertBytesEqual(BuildSampleArchive(), writer.Build());
		}

		TEST_METHOD(MigrationThroughFiles)
		{
			const TempFile oldFile;
			const TempFile newFile;

			ArchiveWriter legacy;
			AssertOk(legacy.SetFormatVersion(1));
			AssertOk(legacy.AddAsset(kTextId, AssetType::Text, Bytes({ 'H', 'e', 'l', 'l', 'o' })));
			AssertOk(legacy.SaveToFile(oldFile.GetPath()));

			ArchiveReader oldArchive;
			AssertOk(oldArchive.OpenFile(oldFile.GetPath()));
			ArchiveWriter writer;
			AssertOk(writer.AddAssets(oldArchive));
			AssertOk(writer.SaveToFile(newFile.GetPath()));

			ArchiveReader upgraded;
			AssertOk(upgraded.OpenFile(newFile.GetPath()));
			Assert::AreEqual(kArchiveVersion, upgraded.GetHeader().version);

			std::span<const std::byte> data;
			AssertOk(upgraded.GetAssetData(kTextId, data));
			AssertBytesEqual(Bytes({ 'H', 'e', 'l', 'l', 'o' }), data);
		}

		TEST_METHOD(DowngradeAndUpgradeKeepTheContent)
		{
			// Current -> version 1 -> current gives back the same bytes.
			ArchiveReader current;
			AssertOk(current.Open(BuildSampleArchive()));

			ArchiveWriter downgrade;
			AssertOk(downgrade.SetFormatVersion(1));
			AssertOk(downgrade.AddAssets(current));

			ArchiveReader version1;
			AssertOk(version1.Open(downgrade.Build()));
			ArchiveWriter upgrade;
			AssertOk(upgrade.AddAssets(version1));

			AssertBytesEqual(BuildSampleArchive(), upgrade.Build());
		}

		TEST_METHOD(AddAssetsRequiresAnOpenArchive)
		{
			const ArchiveReader closed;
			ArchiveWriter writer;
			AssertFails(ArchiveError::NotOpen, writer.AddAssets(closed));
			Assert::AreEqual(std::size_t{ 0 }, writer.GetAssetCount());
		}

		TEST_METHOD(AddAssetsIsAllOrNothing)
		{
			ArchiveReader reader;
			AssertOk(reader.Open(BuildSampleArchive()));

			// The writer already holds one of the three IDs: nothing is added.
			ArchiveWriter writer;
			AssertOk(writer.AddAsset(kTextureId, AssetType::Binary, Bytes({ 1 })));
			AssertFails(ArchiveError::DuplicateAssetId, writer.AddAssets(reader));

			Assert::AreEqual(std::size_t{ 1 }, writer.GetAssetCount());
			Assert::IsFalse(writer.Contains(kTextId));
			Assert::IsFalse(writer.Contains(kEmptyId));
		}

		// --- Unknown versions --------------------------------------------------------------------

		TEST_METHOD(RejectsEveryUnknownVersion)
		{
			const std::vector<std::byte> archive = BuildSampleArchive();
			ArchiveReader reader;

			for (const std::uint16_t version : { std::uint16_t{ 0 }, std::uint16_t{ kArchiveVersion + 1 },
				std::uint16_t{ 100 }, std::uint16_t{ 0x0100 }, std::uint16_t{ 0xFFFF } })
			{
				AssertFails(ArchiveError::UnsupportedVersion, reader.Open(WithVersion(archive, version)),
					(L"Version " + std::to_wstring(version) + L" accepted").c_str());
			}
			Assert::IsFalse(reader.IsOpen());
		}

		TEST_METHOD(VersionChangedToVersion1IsRejected)
		{
			// The version field is read before the header checksum (it selects the layout), so
			// changing 2 into 1 makes the reader use the version-1 layout. It no longer matches the
			// directory, and the structural checks reject the archive (here: an unknown asset type).
			ArchiveReader reader;
			const ArchiveResult result = reader.Open(WithVersion(BuildSampleArchive(), 1));
			Assert::IsFalse(result.IsOk());
			Assert::IsFalse(reader.IsOpen());
		}
	};
}
