#pragma once
#include <cstddef>
#include <filesystem>
#include <span>
#include <string_view>
#include <vector>

namespace asset
{
	class ArchiveReader;
	class ArchiveWriter;
}

/**
 * @file Commands.h
 * @brief The commands of the AssetLoader demo application (see main.cpp for the command line).
 *
 * Each command returns the process exit code: 0 on success, 1 on failure.
 */
namespace app
{
	/** @brief Walk-through of the archive system: write, read, load by ID, damaged archives, versions. */
	int RunDemo(const std::filesystem::path& archivePath);

	/** @brief Writes the same sample assets as a version-1 and a version-2 archive into `directory`. */
	int RunSamples(const std::filesystem::path& directory);

	/** @brief Opens an archive, prints its header and directory, or the reason it was rejected. */
	int RunInspect(const std::filesystem::path& archivePath);

	/** @brief Opens an archive of any supported version and saves it in the current version. */
	int RunMigrate(const std::filesystem::path& source, const std::filesystem::path& destination);

	/** @brief Measures archive size, save/load times and temporary memory for two data volumes. */
	int RunBenchmark();

	// --- Helpers shared by the commands ---------------------------------------------------------

	/** @brief The bytes of a string (without terminating zero). */
	std::vector<std::byte> ToBytes(std::string_view text);

	/** @brief Views bytes as text (for printing text assets). */
	std::string_view ToText(std::span<const std::byte> bytes);

	/** @brief Adds the three sample assets: a text, a 4x4 RGBA texture and a config file. */
	void AddSampleAssets(asset::ArchiveWriter& writer);

	/** @brief Prints the header and the directory of an open archive. */
	void PrintDirectory(const asset::ArchiveReader& reader);
}
