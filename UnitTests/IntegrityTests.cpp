#include "pch.h"
#include "TestHelpers.h"
#include "ArchiveTestHelpers.h"

#include <fstream>

#include "Archive/ArchiveFormat.h"
#include "Archive/ArchiveReader.h"
#include "Archive/ArchiveWriter.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace tests;

namespace IntegrityTests
{
	namespace
	{
		/** Opens `archive` and asserts that it fails with `expected`, and that the reader stays closed. */
		void AssertOpenFails(ArchiveError expected, std::span<const std::byte> archive, const wchar_t* message = nullptr)
		{
			ArchiveReader reader;
			AssertFails(expected, reader.Open(archive), message);
			Assert::IsFalse(reader.IsOpen(), message);
		}

		/** Flips one byte of a file on disk. */
		void CorruptFile(const std::filesystem::path& path, std::size_t index)
		{
			std::fstream file(path, std::ios::binary | std::ios::in | std::ios::out);
			Assert::IsTrue(file.is_open());

			char value = 0;
			file.seekg(static_cast<std::streamoff>(index));
			file.read(&value, 1);
			value = static_cast<char>(~value);
			file.seekp(static_cast<std::streamoff>(index));
			file.write(&value, 1);
			Assert::IsTrue(file.good());
		}
	}

	/*
	* Integrity tests: CRC-32 checks of the header, directory and data, archive size checks
	* (truncated files), and validation of the metadata of every directory entry.
	*/
	TEST_CLASS(ArchiveIntegrityTests)
	{
	public:

		// --- Checksums ----------------------------------------------------------------------

		TEST_METHOD(ValidArchivePassesEveryCheck)
		{
			ArchiveReader reader;
			AssertOk(reader.Open(BuildSampleArchive()));
			AssertSampleContent(reader);
		}

		TEST_METHOD(DetectsEverySingleByteCorruption)
		{
			// Each byte of the archive is covered by a check: changing any of them must be detected.
			const std::vector<std::byte> archive = BuildSampleArchive();
			for (std::size_t index = 0; index < archive.size(); ++index)
			{
				for (const unsigned mask : { 0x01u, 0x80u, 0xFFu })
				{
					ArchiveReader reader;
					const std::vector<std::byte> damaged = Corrupt(archive, index, mask);
					Assert::IsFalse(reader.Open(damaged).IsOk(),
						(L"Corruption not detected at byte " + std::to_wstring(index)).c_str());
				}
			}
		}

		TEST_METHOD(CorruptedHeaderIsReported)
		{
			const std::vector<std::byte> archive = BuildSampleArchive();
			AssertOpenFails(ArchiveError::HeaderChecksumMismatch, Corrupt(archive, 8));  // assetCount
			AssertOpenFails(ArchiveError::HeaderChecksumMismatch, Corrupt(archive, 20)); // archiveSize
			AssertOpenFails(ArchiveError::HeaderChecksumMismatch, Corrupt(archive, 28)); // directoryCrc
			AssertOpenFails(ArchiveError::HeaderChecksumMismatch, Corrupt(archive, 35)); // headerCrc
		}

		TEST_METHOD(CorruptedDirectoryIsReported)
		{
			const std::vector<std::byte> archive = BuildSampleArchive();
			const std::size_t directory = kHeaderSize;

			AssertOpenFails(ArchiveError::DirectoryChecksumMismatch, Corrupt(archive, directory + 0));  // id
			AssertOpenFails(ArchiveError::DirectoryChecksumMismatch, Corrupt(archive, directory + 8));  // type
			AssertOpenFails(ArchiveError::DirectoryChecksumMismatch, Corrupt(archive, directory + 12)); // offset
			AssertOpenFails(ArchiveError::DirectoryChecksumMismatch, Corrupt(archive, directory + 20)); // size
			AssertOpenFails(ArchiveError::DirectoryChecksumMismatch, Corrupt(archive, directory + 28)); // crc32
			AssertOpenFails(ArchiveError::DirectoryChecksumMismatch, Corrupt(archive, directory + 3 * kEntrySize - 1));
		}

		TEST_METHOD(CorruptedAssetDataIsReported)
		{
			const std::vector<std::byte> archive = BuildSampleArchive();
			for (std::size_t index = static_cast<std::size_t>(DataOffset(3)); index < archive.size(); ++index)
			{
				AssertOpenFails(ArchiveError::AssetChecksumMismatch, Corrupt(archive, index, 0x01),
					(L"Wrong error at byte " + std::to_wstring(index)).c_str());
			}
		}

		TEST_METHOD(CorruptedFileOnDiskIsReported)
		{
			ArchiveWriter writer;
			AssertOk(writer.AddAsset(1, AssetType::Text, Bytes({ 'd', 'a', 't', 'a' })));

			const TempFile file;
			AssertOk(writer.SaveToFile(file.GetPath()));
			CorruptFile(file.GetPath(), static_cast<std::size_t>(DataOffset(1)) + 2);

			ArchiveReader reader;
			AssertFails(ArchiveError::AssetChecksumMismatch, reader.OpenFile(file.GetPath()));
		}

		// --- Archive size: truncated files and extra bytes ----------------------------------

		TEST_METHOD(TruncatedFileOnDiskIsReported)
		{
			ArchiveWriter writer;
			AssertOk(writer.AddAsset(1, AssetType::Binary, std::vector<std::byte>(100, std::byte{ 1 })));

			const TempFile file;
			AssertOk(writer.SaveToFile(file.GetPath()));
			std::filesystem::resize_file(file.GetPath(), std::filesystem::file_size(file.GetPath()) - 10);

			ArchiveReader reader;
			AssertFails(ArchiveError::TruncatedArchive, reader.OpenFile(file.GetPath()));
		}

		TEST_METHOD(TrailingBytesAreReported)
		{
			std::vector<std::byte> archive = BuildSampleArchive();
			archive.push_back(std::byte{ 0 });

			AssertOpenFails(ArchiveError::ArchiveSizeMismatch, archive);
		}

		// --- Header metadata ----------------------------------------------------------------

		TEST_METHOD(ReservedFlagsMustBeZero)
		{
			ArchiveHeader header = RawHeader(0);
			header.flags = 1;

			AssertOpenFails(ArchiveError::InvalidHeader, BuildRawArchive(header, {}));
		}

		// --- Entry metadata -----------------------------------------------------------------

		TEST_METHOD(EntryWithInvalidIdIsRejected)
		{
			const std::vector<std::byte> data = Bytes({ 'x' });
			AssertOpenFails(ArchiveError::InvalidAssetId, BuildRawArchive(RawHeader(1),
				{ AssetEntry{ kInvalidAssetId, AssetType::Text, DataOffset(1), 1 } }, data));
		}

		TEST_METHOD(EntryWithInvalidTypeIsRejected)
		{
			const std::vector<std::byte> data = Bytes({ 'x' });
			for (const AssetType type : { AssetType::Unknown, AssetType::Count, static_cast<AssetType>(999) })
			{
				AssertOpenFails(ArchiveError::InvalidAssetType, BuildRawArchive(RawHeader(1),
					{ AssetEntry{ 1, type, DataOffset(1), 1 } }, data));
			}
		}

		TEST_METHOD(EveryValidTypeIsAccepted)
		{
			for (std::uint32_t value = 1; value < static_cast<std::uint32_t>(AssetType::Count); ++value)
			{
				ArchiveWriter writer;
				AssertOk(writer.AddAsset(1, static_cast<AssetType>(value), Bytes({ 1 })));

				ArchiveReader reader;
				AssertOk(reader.Open(writer.Build()));
			}
		}

		TEST_METHOD(DataOverlappingTheHeaderIsRejected)
		{
			const std::vector<std::byte> data = Bytes({ 'x' });
			AssertOpenFails(ArchiveError::OverlappingData, BuildRawArchive(RawHeader(1),
				{ AssetEntry{ 1, AssetType::Binary, 0, 4 } }, data));
			AssertOpenFails(ArchiveError::OverlappingData, BuildRawArchive(RawHeader(1),
				{ AssetEntry{ 1, AssetType::Binary, kHeaderSize - 1, 2 } }, data));
		}

		TEST_METHOD(DataOverlappingTheDirectoryIsRejected)
		{
			const std::vector<std::byte> data = Bytes({ 'x' });
			AssertOpenFails(ArchiveError::OverlappingData, BuildRawArchive(RawHeader(1),
				{ AssetEntry{ 1, AssetType::Binary, kHeaderSize, kEntrySize } }, data));
			AssertOpenFails(ArchiveError::OverlappingData, BuildRawArchive(RawHeader(1),
				{ AssetEntry{ 1, AssetType::Binary, DataOffset(1) - 1, 2 } }, data));
		}

		TEST_METHOD(OverlappingAssetsAreRejected)
		{
			const std::vector<std::byte> data = Bytes({ 'a', 'b', 'c', 'd' });
			const std::uint64_t offset = DataOffset(2);

			// Asset 2 starts inside asset 1.
			AssertOpenFails(ArchiveError::OverlappingData, BuildRawArchive(RawHeader(2), {
				AssetEntry{ 1, AssetType::Binary, offset, 3 },
				AssetEntry{ 2, AssetType::Binary, offset + 2, 2 } }, data));

			// Same range for both (detected even when the directory order differs from the data order).
			AssertOpenFails(ArchiveError::OverlappingData, BuildRawArchive(RawHeader(2), {
				AssetEntry{ 1, AssetType::Binary, offset + 1, 2 },
				AssetEntry{ 2, AssetType::Binary, offset, 4 } }, data));
		}

		TEST_METHOD(AdjacentAndEmptyAssetsAreAccepted)
		{
			const std::vector<std::byte> data = Bytes({ 'a', 'b', 'c', 'd' });
			const std::uint64_t offset = DataOffset(4);

			// Asset 2 starts right where asset 1 ends; empty assets 3 and 4 share offsets with others.
			ArchiveReader reader;
			AssertOk(reader.Open(BuildRawArchive(RawHeader(4), {
				AssetEntry{ 1, AssetType::Binary, offset + 2, 2 },
				AssetEntry{ 2, AssetType::Binary, offset, 2 },
				AssetEntry{ 3, AssetType::Binary, offset, 0 },
				AssetEntry{ 4, AssetType::Binary, offset + 2, 0 } }, data)));

			std::span<const std::byte> view;
			AssertOk(reader.GetAssetData(1, view));
			AssertBytesEqual(Bytes({ 'c', 'd' }), view);
			AssertOk(reader.GetAssetData(2, view));
			AssertBytesEqual(Bytes({ 'a', 'b' }), view);
		}

		TEST_METHOD(FailedIntegrityCheckKeepsThePreviousArchive)
		{
			ArchiveReader reader;
			AssertOk(reader.Open(BuildSampleArchive()));

			const std::vector<std::byte> damaged = Corrupt(BuildSampleArchive(), static_cast<std::size_t>(DataOffset(3)));
			AssertFails(ArchiveError::AssetChecksumMismatch, reader.Open(damaged));
			AssertSampleContent(reader);
		}
	};
}
