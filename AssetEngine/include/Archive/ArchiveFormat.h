#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

#include "Archive/ArchiveResult.h"
#include "Archive/AssetId.h"

/**
 * @file ArchiveFormat.h
 * @brief Binary layout of an asset archive: header, asset directory, data.
 *
 * Every value is encoded with the Byte Access Layer (little-endian, no padding).
 *
 * Archive layout
 * --------------
 *  | Section    | Offset                 | Size                         |
 *  |------------|------------------------|------------------------------|
 *  | Header     | 0                      | kHeaderSize (28 bytes)       |
 *  | Directory  | header.directoryOffset | assetCount * kEntrySize      |
 *  | Asset data | entry.offset           | entry.size, for each asset   |
 *
 * The writer places the directory right after the header and the asset data right after the
 * directory, in the same order as the directory. The reader does not rely on that: it only
 * follows the offsets stored in the file.
 *
 * Header (28 bytes)
 * -----------------
 *  | Offset | Type     | Field           | Value                                              |
 *  |--------|----------|-----------------|----------------------------------------------------|
 *  | 0      | 4 bytes  | magic           | 'A' 'S' 'E' 'T' (41 53 45 54)                      |
 *  | 4      | uint16   | version         | kArchiveVersion                                    |
 *  | 6      | uint16   | flags           | 0 (reserved for future use)                        |
 *  | 8      | uint32   | assetCount      | number of entries in the directory                 |
 *  | 12     | uint64   | directoryOffset | absolute offset of the directory                   |
 *  | 20     | uint64   | archiveSize     | total size of the archive, in bytes                |
 *
 * Directory entry (28 bytes, entries sorted by strictly increasing ID)
 * --------------------------------------------------------------------
 *  | Offset | Type     | Field  | Value                                                      |
 *  |--------|----------|--------|------------------------------------------------------------|
 *  | 0      | uint64   | id     | AssetId (never 0)                                          |
 *  | 8      | uint32   | type   | AssetType                                                  |
 *  | 12     | uint64   | offset | absolute offset of the asset data in the archive           |
 *  | 20     | uint64   | size   | size of the asset data, in bytes (may be 0)                |
 *
 * Sorting the directory by ID lets the reader find an asset with a binary search (O(log n)),
 * without building an index.
 */
namespace asset
{
	class ByteWriter;
	class ByteReader;

	/** @brief Magic Number at the start of every archive: "ASET". */
	inline constexpr std::array<std::byte, 4> kArchiveMagic{
		std::byte{ 'A' }, std::byte{ 'S' }, std::byte{ 'E' }, std::byte{ 'T' } };

	/** @brief Current version of the archive format. Increase it when the layout changes. */
	inline constexpr std::uint16_t kArchiveVersion = 1;

	/** @brief Size of the encoded header, in bytes. */
	inline constexpr std::size_t kHeaderSize = 4 + 2 + 2 + 4 + 8 + 8;

	/** @brief Size of one encoded directory entry, in bytes. */
	inline constexpr std::size_t kEntrySize = 8 + 4 + 8 + 8;

	/**
	 * @brief Archive header, decoded. See the table in the file comment for the encoding.
	 *
	 * The magic number is not stored here: it is constant, written by WriteHeader() and checked by
	 * ReadHeader().
	 */
	struct ArchiveHeader
	{
		std::uint16_t version = kArchiveVersion;
		std::uint16_t flags = 0;
		std::uint32_t assetCount = 0;
		std::uint64_t directoryOffset = kHeaderSize;
		std::uint64_t archiveSize = kHeaderSize;

		[[nodiscard]] friend constexpr bool operator==(const ArchiveHeader&, const ArchiveHeader&) noexcept = default;
	};

	/**
	 * @brief One entry of the asset directory: where an asset is and what it is.
	 */
	struct AssetEntry
	{
		AssetId id = kInvalidAssetId;
		AssetType type = AssetType::Unknown;
		/** @brief Absolute offset of the asset data, from the start of the archive. */
		std::uint64_t offset = 0;
		/** @brief Size of the asset data, in bytes. */
		std::uint64_t size = 0;

		[[nodiscard]] friend constexpr bool operator==(const AssetEntry&, const AssetEntry&) noexcept = default;
	};

	/** @brief Writes the header (magic number included): exactly kHeaderSize bytes. */
	void WriteHeader(ByteWriter& writer, const ArchiveHeader& header);

	/**
	 * @brief Reads and checks a header written by WriteHeader().
	 *
	 * Checks the magic number (InvalidMagic) and the version (UnsupportedVersion). Returns
	 * TruncatedHeader if fewer than kHeaderSize bytes remain.
	 *
	 * Strong guarantee: on failure, `header` is not modified and the reader position does not move.
	 * On success, the position advances by kHeaderSize.
	 */
	ArchiveResult ReadHeader(ByteReader& reader, ArchiveHeader& header) noexcept;

	/** @brief Writes one directory entry: exactly kEntrySize bytes. */
	void WriteEntry(ByteWriter& writer, const AssetEntry& entry);

	/**
	 * @brief Reads one directory entry written by WriteEntry().
	 *
	 * Only decodes the fields; it does not check them against the archive (that is the
	 * ArchiveReader's job). Returns TruncatedDirectory if fewer than kEntrySize bytes remain.
	 *
	 * Strong guarantee: on failure, `entry` is not modified and the reader position does not move.
	 */
	ArchiveResult ReadEntry(ByteReader& reader, AssetEntry& entry) noexcept;
}
