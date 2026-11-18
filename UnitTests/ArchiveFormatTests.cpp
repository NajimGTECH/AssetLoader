#include "pch.h"
#include "TestHelpers.h"

#include "Archive/ArchiveFormat.h"
#include "Archive/AssetId.h"
#include "ByteAccess/ByteReader.h"
#include "ByteAccess/ByteWriter.h"
#include "Integrity/Crc32.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace tests;

namespace ArchiveTests
{
	// Reference values of the 64-bit FNV-1a hash, checked at compile time.
	static_assert(MakeAssetId("") == 0xCBF29CE484222325ull);
	static_assert(MakeAssetId("a") == 0xAF63DC4C8601EC8Cull);
	static_assert(MakeAssetId("foobar") == 0x85944171F73967E8ull);

	/*
	* Format tests: pin down the exact bytes of the header and of a directory entry, and check that
	* ReadHeader / ReadEntry reject damaged input without side effects.
	*/
	TEST_CLASS(ArchiveFormatTests)
	{
	public:

		// --- Constants ----------------------------------------------------------------------

		TEST_METHOD(HeaderAndEntrySizesMatchTheEncoding)
		{
			ByteWriter writer;
			WriteHeader(writer, ArchiveHeader{});
			Assert::AreEqual(kHeaderSize, writer.GetSize());

			writer.Clear();
			WriteEntry(writer, AssetEntry{});
			Assert::AreEqual(kEntrySize, writer.GetSize());
		}

		// --- Asset IDs and types ------------------------------------------------------------

		TEST_METHOD(MakeAssetIdIsDeterministicAndDistinguishesNames)
		{
			Assert::AreEqual(MakeAssetId("textures/player.png"), MakeAssetId("textures/player.png"));
			Assert::AreNotEqual(MakeAssetId("textures/player.png"), MakeAssetId("textures/Player.png"));
			Assert::AreNotEqual(kInvalidAssetId, MakeAssetId(""));
		}

		TEST_METHOD(IsValidAssetType)
		{
			Assert::IsFalse(asset::IsValidAssetType(AssetType::Unknown));
			Assert::IsTrue(asset::IsValidAssetType(AssetType::Binary));
			Assert::IsTrue(asset::IsValidAssetType(AssetType::Scene));
			Assert::IsFalse(asset::IsValidAssetType(AssetType::Count));
			Assert::IsFalse(asset::IsValidAssetType(static_cast<AssetType>(999)));
		}

		// --- Header -------------------------------------------------------------------------

		TEST_METHOD(WriteHeaderExactBytes)
		{
			ArchiveHeader header;
			header.version = 2;
			header.flags = 0;
			header.assetCount = 2;
			header.directoryOffset = 0x24;
			header.archiveSize = 0x0102030405060708ull;
			header.directoryCrc = 0xAABBCCDDu;

			ByteWriter writer;
			WriteHeader(writer, header);

			std::vector<std::byte> expected = Bytes({
				0x41, 0x53, 0x45, 0x54,                         // "ASET"
				0x02, 0x00,                                     // version
				0x00, 0x00,                                     // flags
				0x02, 0x00, 0x00, 0x00,                         // assetCount
				0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // directoryOffset
				0x08, 0x07, 0x06, 0x05, 0x04, 0x03, 0x02, 0x01, // archiveSize
				0xDD, 0xCC, 0xBB, 0xAA,                         // directoryCrc
				});

			// headerCrc: CRC-32 of the 32 bytes above, little-endian.
			const std::uint32_t headerCrc = ComputeCrc32(expected);
			for (int shift = 0; shift < 32; shift += 8)
			{
				expected.push_back(static_cast<std::byte>((headerCrc >> shift) & 0xFFu));
			}

			AssertBytesEqual(expected, writer.GetBuffer());
		}

		TEST_METHOD(HeaderRoundTrip)
		{
			ArchiveHeader written;
			written.flags = 0xABCD;
			written.assetCount = 0xFFFFFFFFu;
			written.directoryOffset = 1234;
			written.archiveSize = 0xFFFFFFFFFFFFFFFFull;
			written.directoryCrc = 0x12345678u;

			ByteWriter writer;
			WriteHeader(writer, written);

			ByteReader reader(writer.GetBuffer());
			ArchiveHeader read;
			AssertOk(ReadHeader(reader, read));
			Assert::IsTrue(written == read);
			Assert::IsTrue(reader.IsAtEnd());
		}

		TEST_METHOD(ReadHeaderRejectsInvalidMagic)
		{
			ByteWriter writer;
			WriteHeader(writer, ArchiveHeader{});
			std::vector<std::byte> bytes = writer.ReleaseBuffer();
			bytes[3] = std::byte{ 'X' };

			ByteReader reader(bytes);
			ArchiveHeader header;
			header.assetCount = 42;

			AssertFails(ArchiveError::InvalidMagic, ReadHeader(reader, header));
			Assert::AreEqual(std::uint32_t{ 42 }, header.assetCount, L"Header modified on failure");
			Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition(), L"Position moved on failure");
		}

		TEST_METHOD(ReadHeaderReportsInvalidMagicBeforeTruncation)
		{
			// A short file that is not an archive at all is reported as "not an archive".
			const std::vector<std::byte> bytes = Bytes({ 'P', 'N', 'G', '!', 0x00 });
			ByteReader reader(bytes);
			ArchiveHeader header;

			AssertFails(ArchiveError::InvalidMagic, ReadHeader(reader, header));
		}

		TEST_METHOD(ReadHeaderRejectsUnsupportedVersion)
		{
			ArchiveHeader future;
			future.version = kArchiveVersion + 1;

			ByteWriter writer;
			WriteHeader(writer, future);

			ByteReader reader(writer.GetBuffer());
			ArchiveHeader header;
			AssertFails(ArchiveError::UnsupportedVersion, ReadHeader(reader, header));
			Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition(), L"Position moved on failure");
		}

		TEST_METHOD(ReadHeaderRejectsTruncatedHeader)
		{
			ByteWriter writer;
			WriteHeader(writer, ArchiveHeader{});
			const std::span<const std::byte> full = writer.GetBuffer();

			// Every prefix shorter than a full header must fail without moving the position.
			for (std::size_t length = 0; length < kHeaderSize; ++length)
			{
				ByteReader reader(full.first(length));
				ArchiveHeader header;
				AssertFails(ArchiveError::TruncatedHeader, ReadHeader(reader, header));
				Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition(), L"Position moved on failure");
			}
		}

		TEST_METHOD(ReadHeaderRejectsCorruptedHeader)
		{
			ArchiveHeader written;
			written.assetCount = 3;
			written.directoryCrc = 0x0BADF00Du;

			ByteWriter writer;
			WriteHeader(writer, written);
			const std::span<const std::byte> valid = writer.GetBuffer();

			// Bytes 0-5 are the magic and the version, checked first. Every other byte, the
			// checksum itself included, is covered by the header checksum.
			for (std::size_t index = 6; index < kHeaderSize; ++index)
			{
				std::vector<std::byte> damaged(valid.begin(), valid.end());
				damaged[index] ^= std::byte{ 0x01 };

				ByteReader reader(damaged);
				ArchiveHeader header;
				header.assetCount = 42;

				AssertFails(ArchiveError::HeaderChecksumMismatch, ReadHeader(reader, header),
					(L"Corruption not detected at byte " + std::to_wstring(index)).c_str());
				Assert::AreEqual(std::uint32_t{ 42 }, header.assetCount, L"Header modified on failure");
				Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition(), L"Position moved on failure");
			}
		}

		TEST_METHOD(ReadHeaderChecksVersionBeforeSize)
		{
			// A version-1 header is shorter (28 bytes): it must be reported as an unsupported
			// version, not as a truncated header.
			std::vector<std::byte> versionOne = Bytes({ 'A', 'S', 'E', 'T', 0x01, 0x00 });
			versionOne.resize(28);

			ByteReader reader(versionOne);
			ArchiveHeader header;
			AssertFails(ArchiveError::UnsupportedVersion, ReadHeader(reader, header));
		}

		// --- Directory entries --------------------------------------------------------------

		TEST_METHOD(WriteEntryExactBytes)
		{
			ByteWriter writer;
			WriteEntry(writer, AssetEntry{ 0x1122334455667788ull, AssetType::Texture, 0x24, 0x10, 0xCAFEBABEu });

			AssertBytesEqual(Bytes({
				0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11, // id
				0x03, 0x00, 0x00, 0x00,                         // type = Texture
				0x24, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // offset
				0x10, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, // size
				0xBE, 0xBA, 0xFE, 0xCA,                         // crc32
				}), writer.GetBuffer());
		}

		TEST_METHOD(EntryRoundTrip)
		{
			const AssetEntry written{ MakeAssetId("mesh"), AssetType::Mesh, 0xFFFFFFFFFFull, 0x123456789ull, 0xFFFFFFFFu };

			ByteWriter writer;
			WriteEntry(writer, written);

			ByteReader reader(writer.GetBuffer());
			AssetEntry read;
			AssertOk(ReadEntry(reader, read));
			Assert::IsTrue(written == read);
			Assert::IsTrue(reader.IsAtEnd());
		}

		TEST_METHOD(ReadEntryKeepsUnknownTypeValues)
		{
			// Decoding never loses information: deciding whether a type is valid is done elsewhere.
			ByteWriter writer;
			WriteEntry(writer, AssetEntry{ 1, static_cast<AssetType>(0xDEADBEEFu), 0, 0 });

			ByteReader reader(writer.GetBuffer());
			AssetEntry read;
			AssertOk(ReadEntry(reader, read));
			Assert::AreEqual(0xDEADBEEFu, static_cast<std::uint32_t>(read.type));
		}

		TEST_METHOD(ReadEntryRejectsTruncatedEntry)
		{
			ByteWriter writer;
			WriteEntry(writer, AssetEntry{ 7, AssetType::Text, 1, 2 });
			const std::span<const std::byte> truncated = writer.GetBuffer().first(kEntrySize - 1);

			ByteReader reader(truncated);
			AssetEntry entry{ 99, AssetType::Audio, 3, 4 };
			const AssetEntry before = entry;

			AssertFails(ArchiveError::TruncatedDirectory, ReadEntry(reader, entry));
			Assert::IsTrue(before == entry, L"Entry modified on failure");
			Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition(), L"Position moved on failure");
		}
	};
}
