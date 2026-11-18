#include "Archive/ArchiveWriter.h"

#include <cstdint>
#include <limits>
#include <utility>

#include "Archive/ArchiveReader.h"
#include "ByteAccess/ByteWriter.h"
#include "FileIO.h"
#include "Integrity/Crc32.h"

namespace asset
{
	ArchiveResult ArchiveWriter::CheckNewAsset(AssetId id, AssetType type) const noexcept
	{
		if (id == kInvalidAssetId)
		{
			return ArchiveError::InvalidAssetId;
		}
		if (!IsValidAssetType(type))
		{
			return ArchiveError::InvalidAssetType;
		}
		if (m_assets.contains(id))
		{
			return ArchiveError::DuplicateAssetId;
		}
		// The asset count is stored as a uint32 in the header.
		if (m_assets.size() >= std::numeric_limits<std::uint32_t>::max())
		{
			return ArchiveError::TooManyAssets;
		}
		return ArchiveError::None;
	}

	ArchiveResult ArchiveWriter::AddAsset(AssetId id, AssetType type, std::span<const std::byte> data)
	{
		if (const ArchiveResult result = CheckNewAsset(id, type); !result)
		{
			return result; // checked before copying, so a rejected asset costs nothing
		}

		return AddAsset(id, type, std::vector<std::byte>(data.begin(), data.end()));
	}

	ArchiveResult ArchiveWriter::AddAsset(AssetId id, AssetType type, std::vector<std::byte>&& data)
	{
		if (const ArchiveResult result = CheckNewAsset(id, type); !result)
		{
			return result; // `data` was not moved from
		}

		// The checksum is computed once here: the data cannot change after it is added.
		const std::uint32_t crc32 = ComputeCrc32(data);
		m_assets.emplace(id, PendingAsset{ type, crc32, std::move(data) });
		return ArchiveError::None;
	}

	ArchiveResult ArchiveWriter::AddAssets(const ArchiveReader& reader)
	{
		if (!reader.IsOpen())
		{
			return ArchiveError::NotOpen;
		}

		// Everything is checked before anything is added. The reader already validated IDs and
		// types and guarantees unique IDs, so only conflicts with this writer remain.
		const std::span<const AssetEntry> entries = reader.GetEntries();
		if (entries.size() > std::numeric_limits<std::uint32_t>::max() - m_assets.size())
		{
			return ArchiveError::TooManyAssets;
		}
		for (const AssetEntry& entry : entries)
		{
			if (m_assets.contains(entry.id))
			{
				return ArchiveError::DuplicateAssetId;
			}
		}

		// Copies go to a separate map first: if an allocation throws, the writer is unchanged.
		std::map<AssetId, PendingAsset> added;
		for (const AssetEntry& entry : entries)
		{
			std::span<const std::byte> data;
			static_cast<void>(reader.GetAssetData(entry.id, data)); // cannot fail: the ID comes from the reader

			// A verified checksum is reused; version 1 stores none, so it is computed.
			const std::uint32_t crc32 = reader.HasChecksums() ? entry.crc32 : ComputeCrc32(data);
			added.emplace(entry.id, PendingAsset{ entry.type, crc32, std::vector<std::byte>(data.begin(), data.end()) });
		}

		m_assets.merge(added); // moves the nodes, no allocation
		return ArchiveError::None;
	}

	ArchiveResult ArchiveWriter::SetFormatVersion(std::uint16_t version) noexcept
	{
		if (!IsSupportedArchiveVersion(version))
		{
			return ArchiveError::UnsupportedVersion;
		}
		m_version = version;
		return ArchiveError::None;
	}

	std::uint16_t ArchiveWriter::GetFormatVersion() const noexcept
	{
		return m_version;
	}

	bool ArchiveWriter::Contains(AssetId id) const noexcept
	{
		return m_assets.contains(id);
	}

	std::size_t ArchiveWriter::GetAssetCount() const noexcept
	{
		return m_assets.size();
	}

	void ArchiveWriter::Clear() noexcept
	{
		m_assets.clear();
	}

	std::vector<std::byte> ArchiveWriter::Build() const
	{
		// Layout: header | directory | data of each asset, in directory (ID) order.
		const std::uint64_t directoryOffset = GetHeaderSize(m_version);
		const std::uint64_t dataOffset = directoryOffset + std::uint64_t{ m_assets.size() } * GetEntrySize(m_version);

		std::uint64_t archiveSize = dataOffset;
		for (const auto& [id, pending] : m_assets)
		{
			archiveSize += pending.data.size();
		}

		// The directory is encoded first, because its checksum is stored in the header.
		ByteWriter directory(static_cast<std::size_t>(dataOffset - directoryOffset));
		std::uint64_t nextDataOffset = dataOffset;
		for (const auto& [id, pending] : m_assets)
		{
			WriteEntry(directory, AssetEntry{ id, pending.type, nextDataOffset, pending.data.size(), pending.crc32 }, m_version);
			nextDataOffset += pending.data.size();
		}

		ArchiveHeader header;
		header.version = m_version;
		header.flags = 0;
		header.assetCount = static_cast<std::uint32_t>(m_assets.size()); // bounded by CheckNewAsset
		header.directoryOffset = directoryOffset;
		header.archiveSize = archiveSize;
		header.directoryCrc = ComputeCrc32(directory.GetBuffer()); // not written in version 1

		// The whole archive is in memory anyway, so its size fits in a size_t. Reserving the exact
		// size up front means the archive buffer is allocated once, with no reallocation.
		ByteWriter writer(static_cast<std::size_t>(archiveSize));
		WriteHeader(writer, header); // computes the header checksum (version 2+)
		writer.WriteBytes(directory.GetBuffer());

		for (const auto& [id, pending] : m_assets)
		{
			writer.WriteBytes(pending.data);
		}

		return writer.ReleaseBuffer();
	}

	ArchiveResult ArchiveWriter::SaveToFile(const std::filesystem::path& path) const
	{
		const std::vector<std::byte> archive = Build();
		return detail::WriteWholeFile(path, archive);
	}
}
