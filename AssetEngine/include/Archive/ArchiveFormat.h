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
 *  | Header     | 0                      | kHeaderSize (36 bytes)       |
 *  | Directory  | header.directoryOffset | assetCount * kEntrySize      |
 *  | Asset data | entry.offset           | entry.size, for each asset   |
 *
 * The writer places the directory right after the header and the asset data right after the
 * directory, in the same order as the directory. The reader does not rely on that: it only
 * follows the offsets stored in the file.
 *
 * Header (36 bytes)
 * -----------------
 *  | Offset | Type     | Field           | Value                                              |
 *  |--------|----------|-----------------|----------------------------------------------------|
 *  | 0      | 4 bytes  | magic           | 'A' 'S' 'E' 'T' (41 53 45 54)                      |
 *  | 4      | uint16   | version         | kArchiveVersion                                    |
 *  | 6      | uint16   | flags           | 0 (reserved for future use)                        |
 *  | 8      | uint32   | assetCount      | number of entries in the directory                 |
 *  | 12     | uint64   | directoryOffset | absolute offset of the directory                   |
 *  | 20     | uint64   | archiveSize     | total size of the archive, in bytes                |
 *  | 28     | uint32   | directoryCrc    | CRC-32 of the whole directory (all entries)        |
 *  | 32     | uint32   | headerCrc       | CRC-32 of the 32 header bytes before this field    |
 *
 * Directory entry (32 bytes, entries sorted by strictly increasing ID)
 * --------------------------------------------------------------------
 *  | Offset | Type     | Field  | Value                                                      |
 *  |--------|----------|--------|------------------------------------------------------------|
 *  | 0      | uint64   | id     | AssetId (never 0)                                          |
 *  | 8      | uint32   | type   | AssetType                                                  |
 *  | 12     | uint64   | offset | absolute offset of the asset data in the archive           |
 *  | 20     | uint64   | size   | size of the asset data, in bytes (may be 0)                |
 *  | 28     | uint32   | crc32  | CRC-32 of the asset data                                   |
 *
 * Sorting the directory by ID lets the reader find an asset with a binary search (O(log n)),
 * without building an index.
 *
 * Integrity
 * ---------
 * Every byte of an archive is covered by a CRC-32 (see Integrity/Crc32.h): the header by
 * headerCrc, the directory by directoryCrc, and each asset by its entry's crc32. archiveSize lets
 * the reader tell a truncated file (or one with extra bytes) from a corrupted one.
 *
 * Version history
 * ---------------
 *  - 1: header (28 bytes) and directory entries (28 bytes) without checksums.
 *  - 2: adds directoryCrc / headerCrc to the header and crc32 to each entry.
 */
namespace asset
{
	class ByteWriter;
	class ByteReader;

	/** @brief Magic Number at the start of every archive: "ASET". */
	inline constexpr std::array<std::byte, 4> kArchiveMagic{
		std::byte{ 'A' }, std::byte{ 'S' }, std::byte{ 'E' }, std::byte{ 'T' } };

	/** @brief Current version of the archive format. Increase it when the layout changes. */
	inline constexpr std::uint16_t kArchiveVersion = 2;

	/** @brief Size of the encoded header, in bytes. */
	inline constexpr std::size_t kHeaderSize = 4 + 2 + 2 + 4 + 8 + 8 + 4 + 4;

	/** @brief Number of header bytes covered by headerCrc (everything before the headerCrc field). */
	inline constexpr std::size_t kHeaderCrcCoveredSize = kHeaderSize - 4;

	/** @brief Size of one encoded directory entry, in bytes. */
	inline constexpr std::size_t kEntrySize = 8 + 4 + 8 + 8 + 4;

	/**
	 * @brief Archive header, decoded. See the table in the file comment for the encoding.
	 *
	 * The magic number and the header checksum are not stored here: they are computed by
	 * WriteHeader() and checked by ReadHeader().
	 */
	struct ArchiveHeader
	{
		std::uint16_t version = kArchiveVersion;
		std::uint16_t flags = 0;
		std::uint32_t assetCount = 0;
		std::uint64_t directoryOffset = kHeaderSize;
		std::uint64_t archiveSize = kHeaderSize;
		/** @brief CRC-32 of the assetCount * kEntrySize directory bytes. */
		std::uint32_t directoryCrc = 0;

		[[nodiscard]] friend constexpr bool operator==(const ArchiveHeader&, const ArchiveHeader&) noexcept = default;
	};

	/**
	 * @brief One entry of the asset directory: where an asset is, what it is, and its checksum.
	 */
	struct AssetEntry
	{
		AssetId id = kInvalidAssetId;
		AssetType type = AssetType::Unknown;
		/** @brief Absolute offset of the asset data, from the start of the archive. */
		std::uint64_t offset = 0;
		/** @brief Size of the asset data, in bytes. */
		std::uint64_t size = 0;
		/** @brief CRC-32 of the asset data. */
		std::uint32_t crc32 = 0;

		[[nodiscard]] friend constexpr bool operator==(const AssetEntry&, const AssetEntry&) noexcept = default;
	};

	/**
	 * @brief Writes the header: exactly kHeaderSize bytes.
	 *
	 * The magic number and the header checksum (headerCrc) are computed here, so the written header
	 * is always self-consistent.
	 */
	void WriteHeader(ByteWriter& writer, const ArchiveHeader& header);

	/**
	 * @brief Reads and checks a header written by WriteHeader().
	 *
	 * Checks, in this order:
	 *  1. the magic number (InvalidMagic; TruncatedHeader if fewer than 4 bytes),
	 *  2. the version (UnsupportedVersion),
	 *  3. that the whole header is present (TruncatedHeader),
	 *  4. the header checksum (HeaderChecksumMismatch).
	 * The magic and version are checked before the size, so a file of another format or of another
	 * version is reported as such, even if its header is shorter.
	 *
	 * The other fields are only decoded: checking them against the archive is the ArchiveReader's job.
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
