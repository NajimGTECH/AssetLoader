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
 * Version history and compatibility
 * ---------------------------------
 *  - 1: header (28 bytes) and directory entries (28 bytes) without checksums: the same tables
 *       without directoryCrc, headerCrc and crc32.
 *  - 2 (current): adds directoryCrc / headerCrc to the header and crc32 to each entry. The new
 *       fields are appended at the end of the header and of each entry; nothing else moved.
 *
 * The reader loads every version from kOldestArchiveVersion to kArchiveVersion. When it reads a
 * version-1 archive, the fields added in version 2 get their default value (0) and the checksum
 * checks are skipped (see HasChecksums()); all other checks still apply. Any other version is
 * rejected with UnsupportedVersion.
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

	/** @brief Oldest version the reader can still load. */
	inline constexpr std::uint16_t kOldestArchiveVersion = 1;

	/** @brief True if the reader can load archives of this version. */
	[[nodiscard]] constexpr bool IsSupportedArchiveVersion(std::uint16_t version) noexcept
	{
		return version >= kOldestArchiveVersion && version <= kArchiveVersion;
	}

	/** @brief True if archives of this version store checksums (version 2 and later). */
	[[nodiscard]] constexpr bool HasChecksums(std::uint16_t version) noexcept
	{
		return version >= 2;
	}

	/** @brief Size of the encoded header for a format version: 28 bytes in version 1, 36 since version 2. */
	[[nodiscard]] constexpr std::size_t GetHeaderSize(std::uint16_t version) noexcept
	{
		return 4 + 2 + 2 + 4 + 8 + 8 + (HasChecksums(version) ? 4 + 4 : 0);
	}

	/** @brief Size of one encoded directory entry for a format version: 28 bytes in version 1, 32 since version 2. */
	[[nodiscard]] constexpr std::size_t GetEntrySize(std::uint16_t version) noexcept
	{
		return 8 + 4 + 8 + 8 + (HasChecksums(version) ? 4 : 0);
	}

	/** @brief Size of the encoded header in the current version, in bytes. */
	inline constexpr std::size_t kHeaderSize = GetHeaderSize(kArchiveVersion);

	/** @brief Number of header bytes covered by headerCrc (everything before the headerCrc field). */
	inline constexpr std::size_t kHeaderCrcCoveredSize = kHeaderSize - 4;

	/** @brief Size of one encoded directory entry in the current version, in bytes. */
	inline constexpr std::size_t kEntrySize = GetEntrySize(kArchiveVersion);

	/**
	 * @brief Archive header, decoded. See the table in the file comment for the encoding.
	 *
	 * The magic number and the header checksum are not stored here: they are computed by
	 * WriteHeader() and checked by ReadHeader().
	 */
	struct ArchiveHeader
	{
		/** @brief Format version of the archive. Selects the header and entry layouts. */
		std::uint16_t version = kArchiveVersion;
		std::uint16_t flags = 0;
		std::uint32_t assetCount = 0;
		std::uint64_t directoryOffset = kHeaderSize;
		std::uint64_t archiveSize = kHeaderSize;
		/** @brief CRC-32 of the assetCount * entry size directory bytes. 0 in version 1 (not stored). */
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
		/** @brief CRC-32 of the asset data. 0 in version 1 (not stored). */
		std::uint32_t crc32 = 0;

		[[nodiscard]] friend constexpr bool operator==(const AssetEntry&, const AssetEntry&) noexcept = default;
	};

	/**
	 * @brief Writes the header in the layout of `header.version`: exactly GetHeaderSize(header.version) bytes.
	 *
	 * The magic number and, since version 2, the header checksum (headerCrc) are computed here, so
	 * the written header is always self-consistent. Versions below 2 are written without checksums
	 * (directoryCrc is ignored). Writing an unsupported version is allowed, to produce test data.
	 */
	void WriteHeader(ByteWriter& writer, const ArchiveHeader& header);

	/**
	 * @brief Reads and checks a header written by WriteHeader().
	 *
	 * Checks, in this order:
	 *  1. the magic number (InvalidMagic; TruncatedHeader if fewer than 4 bytes),
	 *  2. the version, which must be supported (UnsupportedVersion),
	 *  3. that the whole header of that version is present (TruncatedHeader),
	 *  4. since version 2, the header checksum (HeaderChecksumMismatch).
	 * The magic and version are checked before the size, so a file of another format or of another
	 * version is reported as such, even if its header is shorter.
	 *
	 * Version 1 headers are migrated while decoding: directoryCrc, which version 1 does not store,
	 * is set to its default value 0.
	 *
	 * The other fields are only decoded: checking them against the archive is the ArchiveReader's job.
	 *
	 * Strong guarantee: on failure, `header` is not modified and the reader position does not move.
	 * On success, the position advances by GetHeaderSize(header.version).
	 */
	ArchiveResult ReadHeader(ByteReader& reader, ArchiveHeader& header) noexcept;

	/**
	 * @brief Writes one directory entry in the layout of `version`: exactly GetEntrySize(version) bytes.
	 *
	 * Versions below 2 do not store crc32.
	 */
	void WriteEntry(ByteWriter& writer, const AssetEntry& entry, std::uint16_t version = kArchiveVersion);

	/**
	 * @brief Reads one directory entry written by WriteEntry() with the same `version`.
	 *
	 * Only decodes the fields; it does not check them against the archive (that is the
	 * ArchiveReader's job). Returns TruncatedDirectory if fewer than GetEntrySize(version) bytes remain.
	 * In version 1, crc32 is not stored and is set to its default value 0.
	 *
	 * Strong guarantee: on failure, `entry` is not modified and the reader position does not move.
	 */
	ArchiveResult ReadEntry(ByteReader& reader, AssetEntry& entry, std::uint16_t version = kArchiveVersion) noexcept;
}
