#include "FileIO.h"

#include <cstdint>
#include <fstream>
#include <ios>
#include <limits>

namespace asset::detail
{
	ArchiveResult ReadWholeFile(const std::filesystem::path& path, std::vector<std::byte>& bytes)
	{
		std::ifstream file(path, std::ios::binary | std::ios::ate);
		if (!file.is_open())
		{
			return ArchiveError::FileOpenFailed;
		}

		// Opened at the end (ios::ate), so the current position is the file size.
		const std::streamoff fileSize = file.tellg();
		if (fileSize < 0)
		{
			return ArchiveError::FileReadFailed;
		}

		// On 32-bit builds a file can be larger than what a std::vector can address.
		const auto size = static_cast<std::uint64_t>(fileSize);
		if (size > std::numeric_limits<std::size_t>::max())
		{
			return ArchiveError::FileReadFailed;
		}

		std::vector<std::byte> content(static_cast<std::size_t>(size));
		file.seekg(0, std::ios::beg);
		if (!content.empty())
		{
			file.read(reinterpret_cast<char*>(content.data()), static_cast<std::streamsize>(content.size()));
		}
		if (!file || static_cast<std::uint64_t>(file.gcount()) != size)
		{
			return ArchiveError::FileReadFailed;
		}

		bytes = std::move(content);
		return ArchiveError::None;
	}

	ArchiveResult WriteWholeFile(const std::filesystem::path& path, std::span<const std::byte> bytes)
	{
		std::ofstream file(path, std::ios::binary | std::ios::trunc);
		if (!file.is_open())
		{
			return ArchiveError::FileOpenFailed;
		}

		if (!bytes.empty())
		{
			file.write(reinterpret_cast<const char*>(bytes.data()), static_cast<std::streamsize>(bytes.size()));
		}
		file.flush();
		if (!file)
		{
			return ArchiveError::FileWriteFailed;
		}

		return ArchiveError::None;
	}
}
