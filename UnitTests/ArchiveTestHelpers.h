#pragma once
#include "CppUnitTest.h"

#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "Archive/ArchiveFormat.h"
#include "Archive/ArchiveReader.h"
#include "Archive/ArchiveWriter.h"
#include "ByteAccess/ByteWriter.h"
#include "Integrity/Crc32.h"
#include "TestHelpers.h"

/**
 * @file ArchiveTestHelpers.h
 * @brief Archives shared by the ArchiveReader and Integrity tests.
 */
namespace tests
{
	inline const asset::AssetId kTextId = asset::MakeAssetId("intro.txt");
	inline const asset::AssetId kTextureId = asset::MakeAssetId("player.png");
	inline const asset::AssetId kEmptyId = asset::MakeAssetId("empty.bin");

	/** @brief A valid archive with three assets of different sizes (one of them empty). */
	inline std::vector<std::byte> BuildSampleArchive()
	{
		asset::ArchiveWriter writer;
		AssertOk(writer.AddAsset(kTextId, asset::AssetType::Text, Bytes({ 'H', 'e', 'l', 'l', 'o' })));
		AssertOk(writer.AddAsset(kTextureId, asset::AssetType::Texture, Bytes({ 0xFF, 0x00, 0x80, 0x7F, 0x01, 0x02, 0x03, 0x04 })));
		AssertOk(writer.AddAsset(kEmptyId, asset::AssetType::Binary, Bytes({})));
		return writer.Build();
	}

	/** @brief Checks that every asset of BuildSampleArchive() can be found with the right content. */
	inline void AssertSampleContent(const asset::ArchiveReader& reader)
	{
		using Microsoft::VisualStudio::CppUnitTestFramework::Assert;

		Assert::IsTrue(reader.IsOpen());
		Assert::AreEqual(std::size_t{ 3 }, reader.GetAssetCount());

		std::span<const std::byte> data;
		AssertOk(reader.GetAssetData(kTextId, data));
		AssertBytesEqual(Bytes({ 'H', 'e', 'l', 'l', 'o' }), data);

		AssertOk(reader.GetAssetData(kTextureId, data));
		AssertBytesEqual(Bytes({ 0xFF, 0x00, 0x80, 0x7F, 0x01, 0x02, 0x03, 0x04 }), data);

		AssertOk(reader.GetAssetData(kEmptyId, data));
		Assert::IsTrue(data.empty());
	}

	/** @brief Header for BuildRawArchive with `count` entries placed right after the header. */
	inline asset::ArchiveHeader RawHeader(std::uint32_t count)
	{
		asset::ArchiveHeader header;
		header.assetCount = count;
		header.directoryOffset = asset::kHeaderSize;
		return header;
	}

	/** @brief Offset of the data section in a raw archive with `count` entries. */
	constexpr std::uint64_t DataOffset(std::uint64_t count)
	{
		return asset::kHeaderSize + count * asset::kEntrySize;
	}

	/**
	 * @brief Hand-built archive, to test damaged metadata that the ArchiveWriter would never produce.
	 *
	 * Writes the header, then `entries` exactly as given (no sorting, no offset computation), then
	 * `data`. So that each test reaches the check it targets, the checksums and the size are made
	 * consistent with the content:
	 *  - header.archiveSize is set to the real size,
	 *  - header.directoryCrc is set to the checksum of the written entries,
	 *  - entry.crc32 is set to the checksum of the bytes the entry points to (when inside the archive).
	 */
	inline std::vector<std::byte> BuildRawArchive(asset::ArchiveHeader header, std::vector<asset::AssetEntry> entries,
		std::span<const std::byte> data = {})
	{
		const auto assemble = [&]()
		{
			asset::ByteWriter writer;
			asset::WriteHeader(writer, header);
			for (const asset::AssetEntry& entry : entries)
			{
				asset::WriteEntry(writer, entry);
			}
			writer.WriteBytes(data);
			return writer.ReleaseBuffer();
		};

		// The entries and header have fixed sizes, so the data lands at the same place in both passes.
		const std::vector<std::byte> draft = assemble();
		const std::uint64_t size = draft.size();
		for (asset::AssetEntry& entry : entries)
		{
			if (entry.offset <= size && entry.size <= size - entry.offset)
			{
				entry.crc32 = asset::ComputeCrc32(std::span<const std::byte>(draft).subspan(
					static_cast<std::size_t>(entry.offset), static_cast<std::size_t>(entry.size)));
			}
		}

		asset::ByteWriter directory;
		for (const asset::AssetEntry& entry : entries)
		{
			asset::WriteEntry(directory, entry);
		}
		header.directoryCrc = asset::ComputeCrc32(directory.GetBuffer());
		header.archiveSize = size;

		return assemble();
	}

	/** @brief Returns a copy of `bytes` with byte `index` XOR-ed with `mask`. */
	inline std::vector<std::byte> Corrupt(std::span<const std::byte> bytes, std::size_t index, unsigned mask = 0xFF)
	{
		std::vector<std::byte> copy(bytes.begin(), bytes.end());
		copy[index] ^= static_cast<std::byte>(mask);
		return copy;
	}
}
