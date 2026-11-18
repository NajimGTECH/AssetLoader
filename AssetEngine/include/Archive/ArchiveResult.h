#pragma once
#include <cstdint>
#include <string_view>

/**
 * @file ArchiveResult.h
 * @brief Result type returned by the Archive layer (ArchiveWriter, ArchiveReader, ArchiveFormat).
 *
 * Same strategy as ReadResult in the Byte Access Layer: a damaged or missing archive is an expected
 * situation for a loader, so errors are reported through a `[[nodiscard]]` status value, not
 * through exceptions. The only exceptions that can escape the Archive layer are allocation
 * failures (std::bad_alloc / std::length_error).
 */
namespace asset
{
	/**
	 * @brief Reason why an Archive operation failed.
	 */
	enum class ArchiveError : std::uint8_t
	{
		/** @brief The operation succeeded. */
		None = 0,

		// --- File system -------------------------------------------------------------------

		/** @brief The file could not be opened (missing file, bad path, no permission...). */
		FileOpenFailed,
		/** @brief The file was opened but could not be read completely. */
		FileReadFailed,
		/** @brief The file could not be written completely. */
		FileWriteFailed,

		// --- Format ------------------------------------------------------------------------

		/** @brief The data does not start with the archive Magic Number: not an archive. */
		InvalidMagic,
		/** @brief The archive was written with a format version this code cannot read. */
		UnsupportedVersion,
		/** @brief The data is too short to contain the archive header. */
		TruncatedHeader,
		/** @brief The asset directory goes past the end of the data. */
		TruncatedDirectory,
		/** @brief The asset directory is inconsistent (IDs not sorted, or duplicated). */
		InvalidDirectory,
		/** @brief An asset's offset / size points outside the archive data. */
		AssetOutOfBounds,

		// --- Assets ------------------------------------------------------------------------

		/** @brief No asset with this ID exists in the archive. */
		AssetNotFound,
		/** @brief An asset with this ID was already added to the archive. */
		DuplicateAssetId,
		/** @brief The asset ID is kInvalidAssetId (0). */
		InvalidAssetId,
		/** @brief The asset type is AssetType::Unknown or not a known type. */
		InvalidAssetType,
		/** @brief The archive cannot hold more assets (the count is stored on 32 bits). */
		TooManyAssets,

		// --- State -------------------------------------------------------------------------

		/** @brief The ArchiveReader has no archive open. */
		NotOpen,
	};

	/**
	 * @brief Returns a short, human-readable description of an ArchiveError, for diagnostics.
	 *
	 * @code
	 * std::cerr << "Cannot open archive: " << asset::ToString(result.GetError()) << '\n';
	 * @endcode
	 */
	[[nodiscard]] constexpr std::string_view ToString(ArchiveError error) noexcept
	{
		switch (error)
		{
		case ArchiveError::None:               return "no error";
		case ArchiveError::FileOpenFailed:     return "the file could not be opened";
		case ArchiveError::FileReadFailed:     return "the file could not be read";
		case ArchiveError::FileWriteFailed:    return "the file could not be written";
		case ArchiveError::InvalidMagic:       return "invalid magic number (not an asset archive)";
		case ArchiveError::UnsupportedVersion: return "unsupported archive format version";
		case ArchiveError::TruncatedHeader:    return "the archive is too short to contain its header";
		case ArchiveError::TruncatedDirectory: return "the asset directory goes past the end of the archive";
		case ArchiveError::InvalidDirectory:   return "the asset directory is invalid (unsorted or duplicated IDs)";
		case ArchiveError::AssetOutOfBounds:   return "an asset lies outside the archive data";
		case ArchiveError::AssetNotFound:      return "no asset with this ID in the archive";
		case ArchiveError::DuplicateAssetId:   return "an asset with this ID already exists";
		case ArchiveError::InvalidAssetId:     return "invalid asset ID (0 is reserved)";
		case ArchiveError::InvalidAssetType:   return "invalid asset type";
		case ArchiveError::TooManyAssets:      return "too many assets in one archive";
		case ArchiveError::NotOpen:            return "no archive is open";
		}
		return "unknown archive error";
	}

	/**
	 * @class ArchiveResult
	 * @brief Status of an Archive operation: success, or the ArchiveError that caused the failure.
	 *
	 * Works like ReadResult: `[[nodiscard]]`, contextually convertible to bool (true = success),
	 * comparable with an ArchiveError.
	 *
	 * @code
	 * asset::ArchiveReader archive;
	 * if (const asset::ArchiveResult result = archive.OpenFile("game.asset"); !result)
	 * {
	 *     std::cerr << asset::ToString(result.GetError()) << '\n';
	 * }
	 * @endcode
	 */
	class [[nodiscard]] ArchiveResult
	{
	public:
		/** @brief Constructs a successful result. */
		constexpr ArchiveResult() noexcept = default;

		/** @brief Constructs a result from an error code (ArchiveError::None means success). Implicit on purpose. */
		constexpr ArchiveResult(ArchiveError error) noexcept : m_error(error) {}

		/** @brief Returns true if the operation succeeded. */
		[[nodiscard]] constexpr bool IsOk() const noexcept { return m_error == ArchiveError::None; }

		/** @brief Same as IsOk(), allows `if (archive.Open(bytes))`. */
		[[nodiscard]] constexpr explicit operator bool() const noexcept { return IsOk(); }

		/** @brief Returns the error code (ArchiveError::None on success). */
		[[nodiscard]] constexpr ArchiveError GetError() const noexcept { return m_error; }

		/** @brief Two results are equal if they carry the same error code. */
		[[nodiscard]] friend constexpr bool operator==(ArchiveResult, ArchiveResult) noexcept = default;

	private:
		ArchiveError m_error = ArchiveError::None;
	};
}
