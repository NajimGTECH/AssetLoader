#pragma once
#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

#include "Archive/ArchiveFormat.h"
#include "Archive/ArchiveResult.h"
#include "Archive/AssetId.h"

/**
 * @file ArchiveReader.h
 * @brief Opens an archive (see ArchiveFormat.h) and loads its assets by ID.
 */
namespace asset
{
	/**
	 * @class ArchiveReader
	 * @brief Reads the header and directory of an archive, then gives access to each asset by ID.
	 *
	 * Opening an archive decodes the header and the whole directory and checks that:
	 *  - the magic number and the version are correct,
	 *  - the directory lies inside the data,
	 *  - the IDs are sorted and unique (so lookups can use a binary search),
	 *  - every asset (offset + size) lies inside the data.
	 *
	 * After a successful Open, looking up an asset is O(log n) and can only fail with AssetNotFound.
	 *
	 * The archive can be read from:
	 *  - a file: OpenFile() loads it into memory owned by the reader;
	 *  - a buffer the reader takes ownership of: Open(std::move(bytes));
	 *  - a buffer the caller keeps: Open(std::span). No copy, but the buffer must outlive the reader.
	 *
	 * @code
	 * asset::ArchiveReader archive;
	 * if (!archive.OpenFile("game.asset")) { ... }
	 *
	 * std::span<const std::byte> texture;
	 * if (archive.GetAssetData(asset::MakeAssetId("player.png"), texture))
	 * {
	 *     // texture points into the archive, no copy
	 * }
	 * @endcode
	 *
	 * Strong guarantee: if Open / OpenFile fails, the reader keeps the archive it had before (if any).
	 * The reader is movable but not copyable (it may own a large buffer).
	 */
	class ArchiveReader
	{
	public:
		ArchiveReader() noexcept = default;

		ArchiveReader(const ArchiveReader&) = delete;
		ArchiveReader& operator=(const ArchiveReader&) = delete;

		/** @brief The moved-from reader is left closed. */
		ArchiveReader(ArchiveReader&& other) noexcept;
		/** @brief The moved-from reader is left closed. */
		ArchiveReader& operator=(ArchiveReader&& other) noexcept;

		~ArchiveReader() = default;

		/**
		 * @brief Opens an archive stored in a buffer owned by the caller (no copy).
		 *
		 * The buffer must stay alive and unchanged until the reader is closed, re-opened or destroyed.
		 */
		ArchiveResult Open(std::span<const std::byte> archive);

		/**
		 * @brief Opens an archive and takes ownership of its bytes (no copy).
		 *
		 * Also selected for a temporary vector, so `reader.Open(LoadBytes())` cannot dangle.
		 * On failure, `archive` is left untouched.
		 */
		ArchiveResult Open(std::vector<std::byte>&& archive);

		/**
		 * @brief Loads the whole file into memory, then opens it.
		 *
		 * @return FileOpenFailed / FileReadFailed for I/O errors, otherwise the same errors as Open().
		 */
		ArchiveResult OpenFile(const std::filesystem::path& path);

		/** @brief Closes the archive and releases the memory it owns. */
		void Close() noexcept;

		/** @brief True if an archive is open. */
		[[nodiscard]] bool IsOpen() const noexcept;

		/** @brief Header of the open archive (default header if none is open). */
		[[nodiscard]] const ArchiveHeader& GetHeader() const noexcept;

		/** @brief Every directory entry, sorted by ID (empty if no archive is open). */
		[[nodiscard]] std::span<const AssetEntry> GetEntries() const noexcept;

		/** @brief Number of assets in the open archive. */
		[[nodiscard]] std::size_t GetAssetCount() const noexcept;

		/** @brief True if the open archive contains an asset with this ID. */
		[[nodiscard]] bool Contains(AssetId id) const noexcept;

		/**
		 * @brief Copies the directory entry of an asset into `entry`.
		 * @return AssetNotFound, or NotOpen. `entry` is only modified on success.
		 */
		ArchiveResult FindEntry(AssetId id, AssetEntry& entry) const noexcept;

		/**
		 * @brief Zero-copy access to an asset's bytes: `data` points into the archive buffer.
		 *
		 * `data` stays valid as long as the archive is open in this reader.
		 * @return AssetNotFound, or NotOpen. `data` is only modified on success.
		 */
		ArchiveResult GetAssetData(AssetId id, std::span<const std::byte>& data) const noexcept;

		/**
		 * @brief Copies an asset's bytes into `data` (replacing its content).
		 *
		 * Use it when the bytes must outlive the reader.
		 * @return AssetNotFound, or NotOpen. `data` is only modified on success.
		 */
		ArchiveResult LoadAsset(AssetId id, std::vector<std::byte>& data) const;

	private:
		/** @brief Binary search in m_entries. Returns nullptr if the ID is not there. */
		[[nodiscard]] const AssetEntry* Find(AssetId id) const noexcept;

		/** @brief Decodes and checks `archive`. On success, fills `header` and `entries`. */
		[[nodiscard]] static ArchiveResult Parse(std::span<const std::byte> archive,
			ArchiveHeader& header, std::vector<AssetEntry>& entries);

		/** @brief Bytes owned by the reader (empty when the archive is a caller-owned span). */
		std::vector<std::byte> m_storage;
		/** @brief The open archive: either a view of m_storage or of the caller's buffer. */
		std::span<const std::byte> m_archive;
		ArchiveHeader m_header;
		std::vector<AssetEntry> m_entries;
		bool m_isOpen = false;
	};
}
