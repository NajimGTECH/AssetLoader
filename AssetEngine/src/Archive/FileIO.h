#pragma once
#include <cstddef>
#include <filesystem>
#include <span>
#include <vector>

#include "Archive/ArchiveResult.h"

/**
 * @file FileIO.h
 * @brief Private helpers to read / write a whole file as bytes.
 *
 * Not part of the public API (lives in src/, not include/).
 */
namespace asset::detail
{
	/**
	 * @brief Reads the whole file at `path` into `bytes` (replacing its content).
	 *
	 * @return FileOpenFailed if the file cannot be opened, FileReadFailed if it cannot be read
	 *         completely or does not fit in memory. `bytes` is only modified on success.
	 */
	ArchiveResult ReadWholeFile(const std::filesystem::path& path, std::vector<std::byte>& bytes);

	/**
	 * @brief Writes `bytes` to `path`, replacing any existing file.
	 *
	 * @return FileOpenFailed if the file cannot be created, FileWriteFailed if it cannot be
	 *         written completely.
	 */
	ArchiveResult WriteWholeFile(const std::filesystem::path& path, std::span<const std::byte> bytes);
}
