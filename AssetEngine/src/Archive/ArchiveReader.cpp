#include "Archive/ArchiveReader.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "ByteAccess/ByteReader.h"
#include "FileIO.h"
#include "Integrity/Crc32.h"

namespace asset
{
	namespace
	{
		/**
		 * @brief True if [offset, offset + size) lies inside a buffer of `bufferSize` bytes.
		 *
		 * Written as `size <= bufferSize - offset` so that huge values read from a corrupted file
		 * cannot overflow. Done in 64 bits, so it is also correct on 32-bit builds.
		 */
		[[nodiscard]] constexpr bool IsRangeInside(std::uint64_t offset, std::uint64_t size, std::uint64_t bufferSize) noexcept
		{
			return offset <= bufferSize && size <= bufferSize - offset;
		}

		/**
		 * @brief True if [aOffset, aOffset + aSize) and [bOffset, bOffset + bSize) share at least one byte.
		 *
		 * Empty ranges never overlap anything. Both ranges must already be inside the archive, so
		 * the additions cannot overflow.
		 */
		[[nodiscard]] constexpr bool RangesOverlap(std::uint64_t aOffset, std::uint64_t aSize,
			std::uint64_t bOffset, std::uint64_t bSize) noexcept
		{
			return aSize != 0 && bSize != 0 && aOffset < bOffset + bSize && bOffset < aOffset + aSize;
		}

		/**
		 * @brief Checks one directory entry against the archive and the entries before it.
		 *
		 * `previous` holds the entries already validated, in directory order.
		 */
		[[nodiscard]] ArchiveResult ValidateEntry(const AssetEntry& entry, const std::vector<AssetEntry>& previous,
			std::uint64_t directoryOffset, std::uint64_t directorySize, std::uint64_t archiveSize) noexcept
		{
			if (entry.id == kInvalidAssetId)
			{
				return ArchiveError::InvalidAssetId;
			}
			if (!IsValidAssetType(entry.type))
			{
				return ArchiveError::InvalidAssetType;
			}

			// Strictly increasing IDs: sorted (binary search works) and no duplicates.
			if (!previous.empty() && entry.id <= previous.back().id)
			{
				return ArchiveError::InvalidDirectory;
			}

			if (!IsRangeInside(entry.offset, entry.size, archiveSize))
			{
				return ArchiveError::AssetOutOfBounds;
			}

			// Asset data must not be read from the header or the directory.
			if (RangesOverlap(entry.offset, entry.size, 0, kHeaderSize)
				|| RangesOverlap(entry.offset, entry.size, directoryOffset, directorySize))
			{
				return ArchiveError::OverlappingData;
			}

			return ArchiveError::None;
		}

		/**
		 * @brief True if the data of two assets share at least one byte.
		 *
		 * Sorts the non-empty ranges by offset, then compares each one with the next: O(n log n).
		 * Empty assets are ignored (the writer may place them at the same offset as the next asset).
		 */
		[[nodiscard]] bool HasOverlappingData(const std::vector<AssetEntry>& entries)
		{
			struct Range
			{
				std::uint64_t offset;
				std::uint64_t size;
			};

			std::vector<Range> ranges;
			ranges.reserve(entries.size());
			for (const AssetEntry& entry : entries)
			{
				if (entry.size != 0)
				{
					ranges.push_back(Range{ entry.offset, entry.size });
				}
			}

			std::sort(ranges.begin(), ranges.end(),
				[](const Range& a, const Range& b) { return a.offset < b.offset; });

			for (std::size_t i = 1; i < ranges.size(); ++i)
			{
				// Sorted by offset: they overlap if the next one starts before the previous one ends.
				if (ranges[i].offset < ranges[i - 1].offset + ranges[i - 1].size)
				{
					return true;
				}
			}
			return false;
		}
	}

	// --- Construction / move -----------------------------------------------------------------

	ArchiveReader::ArchiveReader(ArchiveReader&& other) noexcept
		: m_storage(std::move(other.m_storage))
		, m_archive(std::exchange(other.m_archive, {}))
		, m_header(std::exchange(other.m_header, {}))
		, m_entries(std::move(other.m_entries))
		, m_isOpen(std::exchange(other.m_isOpen, false))
	{
		// Moving a std::vector transfers its heap block, so m_archive (when it views m_storage)
		// still points to valid memory, now owned by this object.
		other.Close();
	}

	ArchiveReader& ArchiveReader::operator=(ArchiveReader&& other) noexcept
	{
		if (this != &other)
		{
			m_storage = std::move(other.m_storage);
			m_archive = std::exchange(other.m_archive, {});
			m_header = std::exchange(other.m_header, {});
			m_entries = std::move(other.m_entries);
			m_isOpen = std::exchange(other.m_isOpen, false);
			other.Close();
		}
		return *this;
	}

	// --- Opening -----------------------------------------------------------------------------

	ArchiveResult ArchiveReader::Parse(std::span<const std::byte> archive,
		ArchiveHeader& header, std::vector<AssetEntry>& entries)
	{
		ByteReader reader(archive);
		const std::uint64_t actualSize = archive.size();

		// 1. Header: magic, version, checksum (ReadHeader), then field values.
		ArchiveHeader decodedHeader;
		if (const ArchiveResult result = ReadHeader(reader, decodedHeader); !result)
		{
			return result;
		}
		if (decodedHeader.flags != 0)
		{
			return ArchiveError::InvalidHeader;
		}

		// 2. Size: detects truncated files (and extra bytes) before anything else is read.
		//    The header checksum passed, so the stored size itself is trustworthy.
		if (actualSize < decodedHeader.archiveSize)
		{
			return ArchiveError::TruncatedArchive;
		}
		if (actualSize > decodedHeader.archiveSize)
		{
			return ArchiveError::ArchiveSizeMismatch;
		}

		// 3. Directory: after the header, inside the data, checksum OK.
		//    These checks stay even though the size matched: a header with a valid checksum can
		//    still contain wrong offsets (bug in another writer, hand-made file...).
		const std::uint64_t directoryOffset = decodedHeader.directoryOffset;
		const std::uint64_t directorySize = std::uint64_t{ decodedHeader.assetCount } * kEntrySize; // < 2^37, no overflow
		if (directoryOffset < kHeaderSize)
		{
			return ArchiveError::InvalidDirectory;
		}
		if (!IsRangeInside(directoryOffset, directorySize, actualSize))
		{
			return ArchiveError::TruncatedDirectory;
		}

		// Both values are now <= archive.size(), so they fit in a size_t.
		const std::span<const std::byte> directoryBytes =
			archive.subspan(static_cast<std::size_t>(directoryOffset), static_cast<std::size_t>(directorySize));
		if (ComputeCrc32(directoryBytes) != decodedHeader.directoryCrc)
		{
			return ArchiveError::DirectoryChecksumMismatch;
		}

		// 4. Metadata of each entry.
		ByteReader directoryReader(directoryBytes);
		std::vector<AssetEntry> decodedEntries;
		decodedEntries.reserve(decodedHeader.assetCount);

		for (std::uint32_t i = 0; i < decodedHeader.assetCount; ++i)
		{
			AssetEntry entry;
			if (const ArchiveResult result = ReadEntry(directoryReader, entry); !result)
			{
				return result;
			}
			if (const ArchiveResult result = ValidateEntry(entry, decodedEntries, directoryOffset, directorySize, actualSize); !result)
			{
				return result;
			}
			decodedEntries.push_back(entry);
		}

		if (HasOverlappingData(decodedEntries))
		{
			return ArchiveError::OverlappingData;
		}

		// 5. Data: checksum of every asset (bounds were checked in step 4).
		for (const AssetEntry& entry : decodedEntries)
		{
			const std::span<const std::byte> data =
				archive.subspan(static_cast<std::size_t>(entry.offset), static_cast<std::size_t>(entry.size));
			if (ComputeCrc32(data) != entry.crc32)
			{
				return ArchiveError::AssetChecksumMismatch;
			}
		}

		header = decodedHeader;
		entries = std::move(decodedEntries);
		return ArchiveError::None;
	}

	ArchiveResult ArchiveReader::Open(std::span<const std::byte> archive)
	{
		ArchiveHeader header;
		std::vector<AssetEntry> entries;
		if (const ArchiveResult result = Parse(archive, header, entries); !result)
		{
			return result; // the previous archive (if any) stays open
		}

		m_storage = {};
		m_archive = archive;
		m_header = header;
		m_entries = std::move(entries);
		m_isOpen = true;
		return ArchiveError::None;
	}

	ArchiveResult ArchiveReader::Open(std::vector<std::byte>&& archive)
	{
		ArchiveHeader header;
		std::vector<AssetEntry> entries;
		if (const ArchiveResult result = Parse(archive, header, entries); !result)
		{
			return result; // `archive` was not moved from
		}

		m_storage = std::move(archive);
		m_archive = m_storage;
		m_header = header;
		m_entries = std::move(entries);
		m_isOpen = true;
		return ArchiveError::None;
	}

	ArchiveResult ArchiveReader::OpenFile(const std::filesystem::path& path)
	{
		std::vector<std::byte> bytes;
		if (const ArchiveResult result = detail::ReadWholeFile(path, bytes); !result)
		{
			return result;
		}
		return Open(std::move(bytes));
	}

	void ArchiveReader::Close() noexcept
	{
		m_storage = {};
		m_archive = {};
		m_header = {};
		m_entries = {};
		m_isOpen = false;
	}

	// --- Queries -----------------------------------------------------------------------------

	bool ArchiveReader::IsOpen() const noexcept
	{
		return m_isOpen;
	}

	const ArchiveHeader& ArchiveReader::GetHeader() const noexcept
	{
		return m_header;
	}

	std::span<const AssetEntry> ArchiveReader::GetEntries() const noexcept
	{
		return m_entries;
	}

	std::size_t ArchiveReader::GetAssetCount() const noexcept
	{
		return m_entries.size();
	}

	const AssetEntry* ArchiveReader::Find(AssetId id) const noexcept
	{
		const auto it = std::lower_bound(m_entries.begin(), m_entries.end(), id,
			[](const AssetEntry& entry, AssetId value) { return entry.id < value; });

		if (it == m_entries.end() || it->id != id)
		{
			return nullptr;
		}
		return &*it;
	}

	bool ArchiveReader::Contains(AssetId id) const noexcept
	{
		return Find(id) != nullptr;
	}

	ArchiveResult ArchiveReader::FindEntry(AssetId id, AssetEntry& entry) const noexcept
	{
		if (!m_isOpen)
		{
			return ArchiveError::NotOpen;
		}

		const AssetEntry* const found = Find(id);
		if (found == nullptr)
		{
			return ArchiveError::AssetNotFound;
		}

		entry = *found;
		return ArchiveError::None;
	}

	ArchiveResult ArchiveReader::GetAssetData(AssetId id, std::span<const std::byte>& data) const noexcept
	{
		AssetEntry entry;
		if (const ArchiveResult result = FindEntry(id, entry); !result)
		{
			return result;
		}

		// Bounds were checked when the archive was opened, so these casts cannot truncate.
		data = m_archive.subspan(static_cast<std::size_t>(entry.offset), static_cast<std::size_t>(entry.size));
		return ArchiveError::None;
	}

	ArchiveResult ArchiveReader::LoadAsset(AssetId id, std::vector<std::byte>& data) const
	{
		std::span<const std::byte> view;
		if (const ArchiveResult result = GetAssetData(id, view); !result)
		{
			return result;
		}

		data.assign(view.begin(), view.end());
		return ArchiveError::None;
	}
}
