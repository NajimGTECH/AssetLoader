#include "Archive/ArchiveReader.h"

#include <algorithm>
#include <cstdint>
#include <utility>

#include "ByteAccess/ByteReader.h"
#include "FileIO.h"

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

		ArchiveHeader decodedHeader;
		if (const ArchiveResult result = ReadHeader(reader, decodedHeader); !result)
		{
			return result;
		}

		// The directory must start after the header and fit entirely in the data.
		const std::uint64_t archiveSize = archive.size();
		const std::uint64_t directorySize = std::uint64_t{ decodedHeader.assetCount } * kEntrySize; // < 2^37, no overflow
		if (decodedHeader.directoryOffset < kHeaderSize)
		{
			return ArchiveError::InvalidDirectory;
		}
		if (!IsRangeInside(decodedHeader.directoryOffset, directorySize, archiveSize))
		{
			return ArchiveError::TruncatedDirectory;
		}

		// Both values are now <= archive.size(), so they fit in a size_t.
		if (!reader.Seek(static_cast<std::size_t>(decodedHeader.directoryOffset)))
		{
			return ArchiveError::TruncatedDirectory;
		}

		std::vector<AssetEntry> decodedEntries;
		decodedEntries.reserve(decodedHeader.assetCount);

		for (std::uint32_t i = 0; i < decodedHeader.assetCount; ++i)
		{
			AssetEntry entry;
			if (const ArchiveResult result = ReadEntry(reader, entry); !result)
			{
				return result;
			}

			// Strictly increasing IDs: sorted (binary search works) and no duplicates.
			if (!decodedEntries.empty() && entry.id <= decodedEntries.back().id)
			{
				return ArchiveError::InvalidDirectory;
			}

			if (!IsRangeInside(entry.offset, entry.size, archiveSize))
			{
				return ArchiveError::AssetOutOfBounds;
			}

			decodedEntries.push_back(entry);
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
