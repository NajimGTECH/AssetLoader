#pragma once
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <map>
#include <span>
#include <vector>

#include "Archive/ArchiveFormat.h"
#include "Archive/ArchiveResult.h"
#include "Archive/AssetId.h"

/**
 * @file ArchiveWriter.h
 * @brief Collects several assets in memory, then saves them as one archive (see ArchiveFormat.h).
 */
namespace asset
{
	class ArchiveReader;

	/**
	 * @class ArchiveWriter
	 * @brief Builds an archive from several assets.
	 *
	 * Assets are added with AddAsset(), then the whole archive is produced by Build() (in memory) or
	 * SaveToFile() (on disk). The writer keeps its assets, so it can be saved several times.
	 *
	 * @code
	 * asset::ArchiveWriter archive;
	 * if (!archive.AddAsset(asset::MakeAssetId("player.png"), asset::AssetType::Texture, textureBytes)
	 *     || !archive.AddAsset(asset::MakeAssetId("intro.txt"), asset::AssetType::Text, textBytes))
	 * {
	 *     // duplicate ID or invalid metadata
	 * }
	 *
	 * if (const asset::ArchiveResult result = archive.SaveToFile("game.asset"); !result) { ... }
	 * @endcode
	 */
	class ArchiveWriter
	{
	public:
		ArchiveWriter() = default;

		/**
		 * @brief Adds an asset. The data is copied, so `data` can be released afterwards.
		 *
		 * @return InvalidAssetId if `id` is 0, InvalidAssetType if `type` is not a valid AssetType,
		 *         DuplicateAssetId if an asset with this ID was already added, TooManyAssets if the
		 *         archive already holds the maximum number of assets.
		 *         On failure, the writer is not modified.
		 */
		ArchiveResult AddAsset(AssetId id, AssetType type, std::span<const std::byte> data);

		/**
		 * @brief Same as above, but takes ownership of the bytes instead of copying them.
		 *
		 * On failure, `data` is left untouched.
		 */
		ArchiveResult AddAsset(AssetId id, AssetType type, std::vector<std::byte>&& data);

		/**
		 * @brief Adds (copies) every asset of an open archive, whatever its format version.
		 *
		 * Used to migrate an archive to the current version:
		 * @code
		 * asset::ArchiveReader oldArchive;
		 * asset::ArchiveWriter upgraded;
		 * if (oldArchive.OpenFile("old.aset") && upgraded.AddAssets(oldArchive)) { upgraded.SaveToFile("new.aset"); }
		 * @endcode
		 *
		 * @return NotOpen if `reader` has no open archive, DuplicateAssetId if one of its IDs was
		 *         already added, TooManyAssets if the total would exceed the limit.
		 *         All-or-nothing: on failure, the writer is not modified.
		 */
		ArchiveResult AddAssets(const ArchiveReader& reader);

		/**
		 * @brief Chooses the format version written by Build() / SaveToFile(). Default: kArchiveVersion.
		 *
		 * Only needed to produce archives for an older reader (or test data): version 1 has no
		 * checksums, so prefer the current version.
		 *
		 * @return UnsupportedVersion if `version` is not between kOldestArchiveVersion and
		 *         kArchiveVersion (the writer is then not modified).
		 */
		ArchiveResult SetFormatVersion(std::uint16_t version) noexcept;

		/** @brief Format version written by Build() / SaveToFile(). */
		[[nodiscard]] std::uint16_t GetFormatVersion() const noexcept;

		/** @brief True if an asset with this ID was added. */
		[[nodiscard]] bool Contains(AssetId id) const noexcept;

		/** @brief Number of assets added so far. */
		[[nodiscard]] std::size_t GetAssetCount() const noexcept;

		/** @brief Removes every asset (the format version is kept). */
		void Clear() noexcept;

		/**
		 * @brief Encodes the archive (header, directory, data) into a new buffer.
		 *
		 * The directory is sorted by ID. Only fails if memory allocation fails (std::bad_alloc).
		 */
		[[nodiscard]] std::vector<std::byte> Build() const;

		/**
		 * @brief Builds the archive and writes it to `path`. An existing file is replaced.
		 *
		 * @return FileOpenFailed or FileWriteFailed if the file could not be written.
		 */
		ArchiveResult SaveToFile(const std::filesystem::path& path) const;

	private:
		struct PendingAsset
		{
			AssetType type = AssetType::Unknown;
			/** @brief CRC-32 of `data`, computed when the asset is added. */
			std::uint32_t crc32 = 0;
			std::vector<std::byte> data;
		};

		/** @brief Checks the metadata of a new asset (does not modify the writer). */
		[[nodiscard]] ArchiveResult CheckNewAsset(AssetId id, AssetType type) const noexcept;

		// std::map keeps the assets sorted by ID, which is the order required by the directory.
		std::map<AssetId, PendingAsset> m_assets;
		std::uint16_t m_version = kArchiveVersion;
	};
}
