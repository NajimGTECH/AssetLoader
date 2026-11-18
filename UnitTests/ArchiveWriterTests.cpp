#include "pch.h"
#include "TestHelpers.h"

#include <fstream>
#include <iterator>

#include "Archive/ArchiveFormat.h"
#include "Archive/ArchiveWriter.h"
#include "ByteAccess/ByteReader.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace tests;

namespace ArchiveTests
{
	namespace
	{
		/** Decodes the header and the whole directory of a built archive (no validation). */
		void DecodeArchive(std::span<const std::byte> archive, ArchiveHeader& header, std::vector<AssetEntry>& entries)
		{
			ByteReader reader(archive);
			AssertOk(ReadHeader(reader, header));
			AssertOk(reader.Seek(static_cast<std::size_t>(header.directoryOffset)));

			entries.clear();
			for (std::uint32_t i = 0; i < header.assetCount; ++i)
			{
				AssetEntry entry;
				AssertOk(ReadEntry(reader, entry));
				entries.push_back(entry);
			}
		}

		/** Reads a whole file with the standard library, independently of the code under test. */
		std::vector<std::byte> ReadFileBytes(const std::filesystem::path& path)
		{
			std::ifstream file(path, std::ios::binary);
			Assert::IsTrue(file.is_open(), L"File was not created");
			const std::vector<char> chars((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

			std::vector<std::byte> bytes(chars.size());
			for (std::size_t i = 0; i < chars.size(); ++i)
			{
				bytes[i] = static_cast<std::byte>(chars[i]);
			}
			return bytes;
		}
	}

	/*
	* Writer tests: metadata checks when adding assets, and exact layout of the built archive.
	*/
	TEST_CLASS(ArchiveWriterTests)
	{
	public:

		TEST_METHOD(NewWriterIsEmpty)
		{
			const ArchiveWriter writer;
			Assert::AreEqual(std::size_t{ 0 }, writer.GetAssetCount());
			Assert::IsFalse(writer.Contains(1));
		}

		TEST_METHOD(EmptyArchiveIsHeaderOnly)
		{
			const ArchiveWriter writer;
			const std::vector<std::byte> archive = writer.Build();

			Assert::AreEqual(kHeaderSize, archive.size());

			ArchiveHeader header;
			std::vector<AssetEntry> entries;
			DecodeArchive(archive, header, entries);
			Assert::AreEqual(kArchiveVersion, header.version);
			Assert::AreEqual(std::uint16_t{ 0 }, header.flags);
			Assert::AreEqual(std::uint32_t{ 0 }, header.assetCount);
			Assert::AreEqual(std::uint64_t{ kHeaderSize }, header.directoryOffset);
			Assert::AreEqual(std::uint64_t{ kHeaderSize }, header.archiveSize);
		}

		TEST_METHOD(AddAssetStoresMetadata)
		{
			ArchiveWriter writer;
			AssertOk(writer.AddAsset(10, AssetType::Text, Bytes({ 'h', 'i' })));
			AssertOk(writer.AddAsset(MakeAssetId("player.png"), AssetType::Texture, Bytes({ 1, 2, 3 })));

			Assert::AreEqual(std::size_t{ 2 }, writer.GetAssetCount());
			Assert::IsTrue(writer.Contains(10));
			Assert::IsTrue(writer.Contains(MakeAssetId("player.png")));
			Assert::IsFalse(writer.Contains(11));
		}

		TEST_METHOD(AddAssetRejectsInvalidId)
		{
			ArchiveWriter writer;
			AssertFails(ArchiveError::InvalidAssetId, writer.AddAsset(kInvalidAssetId, AssetType::Binary, Bytes({ 1 })));
			Assert::AreEqual(std::size_t{ 0 }, writer.GetAssetCount());
		}

		TEST_METHOD(AddAssetRejectsInvalidType)
		{
			ArchiveWriter writer;
			AssertFails(ArchiveError::InvalidAssetType, writer.AddAsset(1, AssetType::Unknown, Bytes({ 1 })));
			AssertFails(ArchiveError::InvalidAssetType, writer.AddAsset(1, AssetType::Count, Bytes({ 1 })));
			AssertFails(ArchiveError::InvalidAssetType, writer.AddAsset(1, static_cast<AssetType>(999), Bytes({ 1 })));
			Assert::AreEqual(std::size_t{ 0 }, writer.GetAssetCount());
		}

		TEST_METHOD(AddAssetRejectsDuplicateId)
		{
			ArchiveWriter writer;
			AssertOk(writer.AddAsset(5, AssetType::Text, Bytes({ 'a' })));
			AssertFails(ArchiveError::DuplicateAssetId, writer.AddAsset(5, AssetType::Mesh, Bytes({ 'b', 'c' })));
			Assert::AreEqual(std::size_t{ 1 }, writer.GetAssetCount());

			// The first asset was not overwritten.
			ArchiveHeader header;
			std::vector<AssetEntry> entries;
			const std::vector<std::byte> archive = writer.Build();
			DecodeArchive(archive, header, entries);
			Assert::AreEqual(std::uint32_t{ static_cast<std::uint32_t>(AssetType::Text) }, static_cast<std::uint32_t>(entries[0].type));
			Assert::AreEqual(std::uint64_t{ 1 }, entries[0].size);
		}

		TEST_METHOD(AddAssetMoveKeepsDataOnFailure)
		{
			ArchiveWriter writer;
			AssertOk(writer.AddAsset(5, AssetType::Text, Bytes({ 'a' })));

			std::vector<std::byte> data = Bytes({ 1, 2, 3 });
			AssertFails(ArchiveError::DuplicateAssetId, writer.AddAsset(5, AssetType::Binary, std::move(data)));
			AssertBytesEqual(Bytes({ 1, 2, 3 }), data);
		}

		TEST_METHOD(AddAssetCopiesTheData)
		{
			std::vector<std::byte> data = Bytes({ 1, 2, 3 });

			ArchiveWriter writer;
			AssertOk(writer.AddAsset(1, AssetType::Binary, std::span<const std::byte>(data)));
			data[0] = std::byte{ 0xFF };

			const std::vector<std::byte> archive = writer.Build();
			AssertBytesEqual(Bytes({ 1, 2, 3 }), std::span<const std::byte>(archive).last(3));
		}

		TEST_METHOD(BuildLayoutIsSortedAndContiguous)
		{
			// Added out of order on purpose: the directory must be sorted by ID.
			ArchiveWriter writer;
			AssertOk(writer.AddAsset(300, AssetType::Audio, Bytes({ 0xC1, 0xC2, 0xC3 })));
			AssertOk(writer.AddAsset(100, AssetType::Text, Bytes({ 0xA1 })));
			AssertOk(writer.AddAsset(200, AssetType::Binary, Bytes({})));

			const std::vector<std::byte> archive = writer.Build();

			ArchiveHeader header;
			std::vector<AssetEntry> entries;
			DecodeArchive(archive, header, entries);

			const std::uint64_t dataOffset = kHeaderSize + 3 * kEntrySize;
			Assert::AreEqual(std::uint32_t{ 3 }, header.assetCount);
			Assert::AreEqual(std::uint64_t{ kHeaderSize }, header.directoryOffset);
			Assert::AreEqual(dataOffset + 4, header.archiveSize);
			Assert::AreEqual(std::uint64_t{ archive.size() }, header.archiveSize);

			Assert::IsTrue(AssetEntry{ 100, AssetType::Text, dataOffset, 1 } == entries[0]);
			Assert::IsTrue(AssetEntry{ 200, AssetType::Binary, dataOffset + 1, 0 } == entries[1]);
			Assert::IsTrue(AssetEntry{ 300, AssetType::Audio, dataOffset + 1, 3 } == entries[2]);

			AssertBytesEqual(Bytes({ 0xA1, 0xC1, 0xC2, 0xC3 }),
				std::span<const std::byte>(archive).subspan(static_cast<std::size_t>(dataOffset)));
		}

		TEST_METHOD(BuildIsRepeatable)
		{
			ArchiveWriter writer;
			AssertOk(writer.AddAsset(1, AssetType::Binary, Bytes({ 1, 2 })));

			const std::vector<std::byte> first = writer.Build();
			const std::vector<std::byte> second = writer.Build();
			AssertBytesEqual(first, second);
		}

		TEST_METHOD(ClearRemovesEveryAsset)
		{
			ArchiveWriter writer;
			AssertOk(writer.AddAsset(1, AssetType::Binary, Bytes({ 1 })));
			writer.Clear();

			Assert::AreEqual(std::size_t{ 0 }, writer.GetAssetCount());
			Assert::AreEqual(kHeaderSize, writer.Build().size());
			AssertOk(writer.AddAsset(1, AssetType::Binary, Bytes({ 1 })), L"ID should be reusable after Clear");
		}

		TEST_METHOD(SaveToFileWritesTheBuiltArchive)
		{
			ArchiveWriter writer;
			AssertOk(writer.AddAsset(1, AssetType::Text, Bytes({ 'a', 'b', 'c' })));
			AssertOk(writer.AddAsset(2, AssetType::Binary, Bytes({ 0x00, 0xFF, 0x0A, 0x0D })));

			const TempFile file;
			AssertOk(writer.SaveToFile(file.GetPath()));
			AssertBytesEqual(writer.Build(), ReadFileBytes(file.GetPath()));
		}

		TEST_METHOD(SaveToFileReplacesAnExistingFile)
		{
			const TempFile file;

			ArchiveWriter big;
			AssertOk(big.AddAsset(1, AssetType::Binary, std::vector<std::byte>(1000, std::byte{ 7 })));
			AssertOk(big.SaveToFile(file.GetPath()));

			const ArchiveWriter empty;
			AssertOk(empty.SaveToFile(file.GetPath()));
			Assert::AreEqual(kHeaderSize, ReadFileBytes(file.GetPath()).size(), L"Old content was not truncated");
		}

		TEST_METHOD(SaveToFileReportsOpenFailure)
		{
			const ArchiveWriter writer;
			const std::filesystem::path path = std::filesystem::temp_directory_path()
				/ L"AssetEngineTest_missing_directory" / L"sub" / L"archive.asset";

			AssertFails(ArchiveError::FileOpenFailed, writer.SaveToFile(path));
		}
	};
}
