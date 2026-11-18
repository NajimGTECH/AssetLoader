#include "Archive/ArchiveWriter.h"

#include <cstdint>
#include <limits>
#include <utility>

#include "ByteAccess/ByteWriter.h"
#include "FileIO.h"

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
		return AddAsset(id, type, std::vector<std::byte>(data.begin(), data.end()));
	}

	ArchiveResult ArchiveWriter::AddAsset(AssetId id, AssetType type, std::vector<std::byte>&& data)
	{
		if (const ArchiveResult result = CheckNewAsset(id, type); !result)
		{
			return result; // `data` was not moved from
		}

		m_assets.emplace(id, PendingAsset{ type, std::move(data) });
		return ArchiveError::None;
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
		const std::uint64_t directoryOffset = kHeaderSize;
		const std::uint64_t dataOffset = directoryOffset + std::uint64_t{ m_assets.size() } * kEntrySize;

		std::uint64_t archiveSize = dataOffset;
		for (const auto& [id, pending] : m_assets)
		{
			archiveSize += pending.data.size();
		}

		ArchiveHeader header;
		header.version = kArchiveVersion;
		header.flags = 0;
		header.assetCount = static_cast<std::uint32_t>(m_assets.size()); // bounded by CheckNewAsset
		header.directoryOffset = directoryOffset;
		header.archiveSize = archiveSize;

		// The whole archive is in memory anyway, so its size fits in a size_t.
		ByteWriter writer(static_cast<std::size_t>(archiveSize));
		WriteHeader(writer, header);

		std::uint64_t nextDataOffset = dataOffset;
		for (const auto& [id, pending] : m_assets)
		{
			WriteEntry(writer, AssetEntry{ id, pending.type, nextDataOffset, pending.data.size() });
			nextDataOffset += pending.data.size();
		}

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
