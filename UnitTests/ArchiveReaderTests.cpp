#include "pch.h"
#include "TestHelpers.h"
#include "ArchiveTestHelpers.h"

#include <utility>

#include "Archive/ArchiveFormat.h"
#include "Archive/ArchiveReader.h"
#include "Archive/ArchiveWriter.h"
#include "ByteAccess/ByteWriter.h"
#include "Integrity/Crc32.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace tests;

namespace ArchiveTests
{
	/*
	* Reader tests: loading assets by ID, and rejecting damaged archives with the right error.
	*/
	TEST_CLASS(ArchiveReaderTests)
	{
	public:

		// --- Loading ------------------------------------------------------------------------

		TEST_METHOD(NewReaderIsClosed)
		{
			const ArchiveReader reader;
			Assert::IsFalse(reader.IsOpen());
			Assert::AreEqual(std::size_t{ 0 }, reader.GetAssetCount());
			Assert::IsTrue(reader.GetEntries().empty());
			Assert::IsFalse(reader.Contains(kTextId));

			AssetEntry entry;
			std::span<const std::byte> data;
			std::vector<std::byte> copy;
			AssertFails(ArchiveError::NotOpen, reader.FindEntry(kTextId, entry));
			AssertFails(ArchiveError::NotOpen, reader.GetAssetData(kTextId, data));
			AssertFails(ArchiveError::NotOpen, reader.LoadAsset(kTextId, copy));
		}

		TEST_METHOD(OpenFromCallerBuffer)
		{
			const std::vector<std::byte> archive = BuildSampleArchive();

			ArchiveReader reader;
			AssertOk(reader.Open(std::span<const std::byte>(archive)));
			AssertSampleContent(reader);

			// Zero-copy: the data points into the caller's buffer.
			std::span<const std::byte> data;
			AssertOk(reader.GetAssetData(kTextId, data));
			Assert::IsTrue(data.data() >= archive.data() && data.data() < archive.data() + archive.size());
		}

		TEST_METHOD(OpenTakesOwnershipOfTemporaryBuffer)
		{
			ArchiveReader reader;
			AssertOk(reader.Open(BuildSampleArchive())); // the temporary is destroyed after this line
			AssertSampleContent(reader);
		}

		TEST_METHOD(HeaderAndEntriesAreExposed)
		{
			ArchiveReader reader;
			AssertOk(reader.Open(BuildSampleArchive()));

			Assert::AreEqual(kArchiveVersion, reader.GetHeader().version);
			Assert::AreEqual(std::uint32_t{ 3 }, reader.GetHeader().assetCount);

			const std::span<const AssetEntry> entries = reader.GetEntries();
			Assert::AreEqual(std::size_t{ 3 }, entries.size());
			Assert::IsTrue(entries[0].id < entries[1].id && entries[1].id < entries[2].id, L"Entries not sorted");

			AssetEntry entry;
			AssertOk(reader.FindEntry(kTextureId, entry));
			Assert::AreEqual(kTextureId, entry.id);
			Assert::AreEqual(static_cast<std::uint32_t>(AssetType::Texture), static_cast<std::uint32_t>(entry.type));
			Assert::AreEqual(std::uint64_t{ 8 }, entry.size);
		}

		TEST_METHOD(LoadAssetCopiesTheBytes)
		{
			std::vector<std::byte> copy = Bytes({ 9, 9, 9, 9, 9, 9, 9, 9, 9, 9 });
			{
				ArchiveReader reader;
				AssertOk(reader.Open(BuildSampleArchive()));
				AssertOk(reader.LoadAsset(kTextId, copy));
			}
			// The reader is gone, the copy is still valid and was fully replaced.
			AssertBytesEqual(Bytes({ 'H', 'e', 'l', 'l', 'o' }), copy);
		}

		TEST_METHOD(UnknownIdIsNotFoundAndOutputsAreUntouched)
		{
			ArchiveReader reader;
			AssertOk(reader.Open(BuildSampleArchive()));

			const AssetId missing = MakeAssetId("missing");
			Assert::IsFalse(reader.Contains(missing));

			AssetEntry entry{ 1, AssetType::Audio, 2, 3 };
			const AssetEntry entryBefore = entry;
			const std::vector<std::byte> marker = Bytes({ 0xAA });
			std::span<const std::byte> data = marker;
			std::vector<std::byte> copy = marker;

			AssertFails(ArchiveError::AssetNotFound, reader.FindEntry(missing, entry));
			AssertFails(ArchiveError::AssetNotFound, reader.GetAssetData(missing, data));
			AssertFails(ArchiveError::AssetNotFound, reader.LoadAsset(missing, copy));

			Assert::IsTrue(entryBefore == entry);
			Assert::IsTrue(data.data() == marker.data());
			AssertBytesEqual(marker, copy);
		}

		TEST_METHOD(EmptyArchive)
		{
			ArchiveReader reader;
			AssertOk(reader.Open(ArchiveWriter{}.Build()));
			Assert::IsTrue(reader.IsOpen());
			Assert::AreEqual(std::size_t{ 0 }, reader.GetAssetCount());

			std::span<const std::byte> data;
			AssertFails(ArchiveError::AssetNotFound, reader.GetAssetData(kTextId, data));
		}

		TEST_METHOD(ManyAssetsAreFoundByBinarySearch)
		{
			ArchiveWriter writer;
			for (std::uint32_t i = 1; i <= 500; ++i)
			{
				// Asset i contains i bytes, each equal to i % 256.
				AssertOk(writer.AddAsset(MakeAssetId("asset" + std::to_string(i)), AssetType::Binary,
					std::vector<std::byte>(i, static_cast<std::byte>(i % 256))));
			}

			ArchiveReader reader;
			AssertOk(reader.Open(writer.Build()));
			Assert::AreEqual(std::size_t{ 500 }, reader.GetAssetCount());

			for (std::uint32_t i = 1; i <= 500; ++i)
			{
				std::span<const std::byte> data;
				AssertOk(reader.GetAssetData(MakeAssetId("asset" + std::to_string(i)), data));
				Assert::AreEqual(std::size_t{ i }, data.size());
				Assert::AreEqual(static_cast<std::byte>(i % 256), data.front());
				Assert::AreEqual(static_cast<std::byte>(i % 256), data.back());
			}
		}

		TEST_METHOD(ReaderDoesNotRequireTheWriterLayout)
		{
			// Directory after the data, assets in a different order than their IDs: still valid.
			const std::vector<std::byte> data = Bytes({ 'B', 'B', 'A' });

			ByteWriter directory;
			WriteEntry(directory, AssetEntry{ 1, AssetType::Text, kHeaderSize + 2, 1, ComputeCrc32(Bytes({ 'A' })) });
			WriteEntry(directory, AssetEntry{ 2, AssetType::Text, kHeaderSize, 2, ComputeCrc32(Bytes({ 'B', 'B' })) });

			ArchiveHeader header;
			header.assetCount = 2;
			header.directoryOffset = kHeaderSize + data.size();
			header.archiveSize = kHeaderSize + data.size() + directory.GetSize();
			header.directoryCrc = ComputeCrc32(directory.GetBuffer());

			ByteWriter writer;
			WriteHeader(writer, header);
			writer.WriteBytes(data);
			writer.WriteBytes(directory.GetBuffer());

			ArchiveReader reader;
			AssertOk(reader.Open(writer.ReleaseBuffer()));

			std::span<const std::byte> view;
			AssertOk(reader.GetAssetData(1, view));
			AssertBytesEqual(Bytes({ 'A' }), view);
			AssertOk(reader.GetAssetData(2, view));
			AssertBytesEqual(Bytes({ 'B', 'B' }), view);
		}

		// --- Files --------------------------------------------------------------------------

		TEST_METHOD(SaveThenOpenFile)
		{
			ArchiveWriter writer;
			AssertOk(writer.AddAsset(kTextId, AssetType::Text, Bytes({ 'H', 'e', 'l', 'l', 'o' })));
			AssertOk(writer.AddAsset(kTextureId, AssetType::Texture, Bytes({ 0xFF, 0x00, 0x80, 0x7F, 0x01, 0x02, 0x03, 0x04 })));
			AssertOk(writer.AddAsset(kEmptyId, AssetType::Binary, Bytes({})));

			const TempFile file;
			AssertOk(writer.SaveToFile(file.GetPath()));

			ArchiveReader reader;
			AssertOk(reader.OpenFile(file.GetPath()));
			AssertSampleContent(reader);
		}

		TEST_METHOD(OpenFileReportsMissingFile)
		{
			const TempFile file; // never created

			ArchiveReader reader;
			AssertFails(ArchiveError::FileOpenFailed, reader.OpenFile(file.GetPath()));
			Assert::IsFalse(reader.IsOpen());
		}

		// --- State --------------------------------------------------------------------------

		TEST_METHOD(CloseReleasesTheArchive)
		{
			ArchiveReader reader;
			AssertOk(reader.Open(BuildSampleArchive()));
			reader.Close();

			Assert::IsFalse(reader.IsOpen());
			Assert::AreEqual(std::size_t{ 0 }, reader.GetAssetCount());
			std::span<const std::byte> data;
			AssertFails(ArchiveError::NotOpen, reader.GetAssetData(kTextId, data));
		}

		TEST_METHOD(FailedOpenKeepsThePreviousArchive)
		{
			ArchiveReader reader;
			AssertOk(reader.Open(BuildSampleArchive()));

			std::vector<std::byte> garbage = Bytes({ 1, 2, 3, 4, 5 });
			AssertFails(ArchiveError::InvalidMagic, reader.Open(std::move(garbage)));
			AssertBytesEqual(Bytes({ 1, 2, 3, 4, 5 }), garbage); // not moved from on failure

			AssertSampleContent(reader);
		}

		TEST_METHOD(MoveTransfersTheArchive)
		{
			ArchiveReader source;
			AssertOk(source.Open(BuildSampleArchive()));

			ArchiveReader moved(std::move(source));
			AssertSampleContent(moved);
			Assert::IsFalse(source.IsOpen()); // moved-from reader is closed

			ArchiveReader assigned;
			assigned = std::move(moved);
			AssertSampleContent(assigned);
			Assert::IsFalse(moved.IsOpen()); // moved-from reader is closed
		}

		// --- Damaged archives ---------------------------------------------------------------

		TEST_METHOD(RejectsEmptyBuffer)
		{
			ArchiveReader reader;
			AssertFails(ArchiveError::TruncatedHeader, reader.Open(std::vector<std::byte>{}));
			Assert::IsFalse(reader.IsOpen());
		}

		TEST_METHOD(RejectsInvalidMagic)
		{
			std::vector<std::byte> archive = BuildSampleArchive();
			archive[0] = std::byte{ 'Z' };

			ArchiveReader reader;
			AssertFails(ArchiveError::InvalidMagic, reader.Open(std::move(archive)));
		}

		TEST_METHOD(RejectsUnsupportedVersion)
		{
			std::vector<std::byte> archive = BuildSampleArchive();
			archive[4] = static_cast<std::byte>(kArchiveVersion + 1); // version, low byte

			ArchiveReader reader;
			AssertFails(ArchiveError::UnsupportedVersion, reader.Open(std::move(archive)));
		}

		TEST_METHOD(RejectsEveryTruncation)
		{
			// Cutting a valid archive anywhere must be detected, never read out of bounds.
			const std::vector<std::byte> archive = BuildSampleArchive();
			for (std::size_t length = 0; length < archive.size(); ++length)
			{
				ArchiveReader reader;
				const ArchiveResult result = reader.Open(std::span<const std::byte>(archive).first(length));
				Assert::IsFalse(result.IsOk(), (L"Truncation not detected at length " + std::to_wstring(length)).c_str());
				Assert::IsFalse(reader.IsOpen());
			}
		}

		TEST_METHOD(TruncationIsReportedAsSuch)
		{
			// Inside the header: TruncatedHeader. Once the header is complete, the stored archive size
			// tells that the file is truncated, wherever the cut is.
			const std::vector<std::byte> archive = BuildSampleArchive();
			const std::span<const std::byte> bytes = archive;

			ArchiveReader reader;
			AssertFails(ArchiveError::TruncatedHeader, reader.Open(bytes.first(kHeaderSize - 1)));
			for (std::size_t length = kHeaderSize; length < archive.size(); ++length)
			{
				AssertFails(ArchiveError::TruncatedArchive, reader.Open(bytes.first(length)),
					(L"Wrong error at length " + std::to_wstring(length)).c_str());
			}
		}

		TEST_METHOD(RejectsDirectoryInsideHeader)
		{
			ArchiveHeader header = RawHeader(0);
			header.directoryOffset = kHeaderSize - 1;

			ArchiveReader reader;
			AssertFails(ArchiveError::InvalidDirectory, reader.Open(BuildRawArchive(header, {})));
		}

		TEST_METHOD(RejectsHugeAssetCount)
		{
			// A corrupted count must not make the reader allocate or read 4 billion entries.
			ArchiveReader reader;
			AssertFails(ArchiveError::TruncatedDirectory, reader.Open(BuildRawArchive(RawHeader(0xFFFFFFFFu), {})));
		}

		TEST_METHOD(RejectsHugeDirectoryOffset)
		{
			ArchiveHeader header = RawHeader(1);
			header.directoryOffset = 0xFFFFFFFFFFFFFFF0ull;

			ArchiveReader reader;
			AssertFails(ArchiveError::TruncatedDirectory,
				reader.Open(BuildRawArchive(header, { AssetEntry{ 1, AssetType::Text, DataOffset(1), 0 } })));
		}

		TEST_METHOD(RejectsUnsortedIds)
		{
			const std::vector<std::byte> data = Bytes({ 'a', 'b' });
			const std::uint64_t offset = DataOffset(2);

			ArchiveReader reader;
			AssertFails(ArchiveError::InvalidDirectory, reader.Open(BuildRawArchive(RawHeader(2), {
				AssetEntry{ 2, AssetType::Text, offset, 1 },
				AssetEntry{ 1, AssetType::Text, offset + 1, 1 } }, data)));
		}

		TEST_METHOD(RejectsDuplicateIds)
		{
			const std::vector<std::byte> data = Bytes({ 'a', 'b' });
			const std::uint64_t offset = DataOffset(2);

			ArchiveReader reader;
			AssertFails(ArchiveError::InvalidDirectory, reader.Open(BuildRawArchive(RawHeader(2), {
				AssetEntry{ 7, AssetType::Text, offset, 1 },
				AssetEntry{ 7, AssetType::Text, offset + 1, 1 } }, data)));
		}

		TEST_METHOD(RejectsAssetOutsideTheData)
		{
			const std::vector<std::byte> data = Bytes({ 'a', 'b', 'c', 'd' });
			const std::uint64_t offset = DataOffset(1);

			ArchiveReader reader;
			// Last byte of the asset is one past the end.
			AssertFails(ArchiveError::AssetOutOfBounds, reader.Open(BuildRawArchive(RawHeader(1),
				{ AssetEntry{ 1, AssetType::Binary, offset + 1, 4 } }, data)));
			// Offset past the end.
			AssertFails(ArchiveError::AssetOutOfBounds, reader.Open(BuildRawArchive(RawHeader(1),
				{ AssetEntry{ 1, AssetType::Binary, offset + 5, 0 } }, data)));
			// Exactly at the end with size 0 is valid.
			AssertOk(reader.Open(BuildRawArchive(RawHeader(1),
				{ AssetEntry{ 1, AssetType::Binary, offset + 4, 0 } }, data)));
		}

		TEST_METHOD(RejectsOffsetPlusSizeOverflow)
		{
			// offset + size wraps around to a small number: a naive `offset + size <= length` check would accept it.
			const std::vector<std::byte> data = Bytes({ 'a', 'b' });

			ArchiveReader reader;
			AssertFails(ArchiveError::AssetOutOfBounds, reader.Open(BuildRawArchive(RawHeader(1),
				{ AssetEntry{ 1, AssetType::Binary, DataOffset(1), 0xFFFFFFFFFFFFFFFFull } }, data)));
			AssertFails(ArchiveError::AssetOutOfBounds, reader.Open(BuildRawArchive(RawHeader(1),
				{ AssetEntry{ 1, AssetType::Binary, 0xFFFFFFFFFFFFFFFFull, 2 } }, data)));
		}
	};
}
