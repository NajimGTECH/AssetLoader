#include "Archive/ArchiveFormat.h"

#include <algorithm>

#include "ByteAccess/ByteReader.h"
#include "ByteAccess/ByteWriter.h"
#include "Integrity/Crc32.h"

namespace asset
{
	namespace
	{
		/** @brief Restores the reader position when a multi-field read fails half-way (strong guarantee). */
		ArchiveResult Rewind(ByteReader& reader, std::size_t start, ArchiveError error) noexcept
		{
			// `start` was a valid position before the read, so Seek cannot fail.
			static_cast<void>(reader.Seek(start));
			return error;
		}
	}

	// --- Header ------------------------------------------------------------------------------

	void WriteHeader(ByteWriter& writer, const ArchiveHeader& header)
	{
		const std::size_t start = writer.GetSize();

		writer.WriteBytes(kArchiveMagic);
		writer.WriteUInt16(header.version);
		writer.WriteUInt16(header.flags);
		writer.WriteUInt32(header.assetCount);
		writer.WriteUInt64(header.directoryOffset);
		writer.WriteUInt64(header.archiveSize);
		writer.WriteUInt32(header.directoryCrc);

		// The header checksum covers every header byte written above (magic included).
		const std::uint32_t headerCrc = ComputeCrc32(writer.GetBuffer().subspan(start, kHeaderCrcCoveredSize));
		writer.WriteUInt32(headerCrc);
	}

	ArchiveResult ReadHeader(ByteReader& reader, ArchiveHeader& header) noexcept
	{
		const std::size_t start = reader.GetPosition();

		// 1. Magic number: a file that is not an archive at all is reported as InvalidMagic,
		//    even if it is shorter than a header.
		std::span<const std::byte> magic;
		if (!reader.ReadBytesView(kArchiveMagic.size(), magic))
		{
			return ArchiveError::TruncatedHeader;
		}
		if (!std::equal(magic.begin(), magic.end(), kArchiveMagic.begin()))
		{
			return Rewind(reader, start, ArchiveError::InvalidMagic);
		}

		// 2. Version, before the size: an archive of another version may have another header size.
		ArchiveHeader decoded;
		if (!reader.ReadUInt16(decoded.version))
		{
			return Rewind(reader, start, ArchiveError::TruncatedHeader);
		}
		if (decoded.version != kArchiveVersion)
		{
			return Rewind(reader, start, ArchiveError::UnsupportedVersion);
		}

		// 3. The rest of the header.
		std::uint32_t storedHeaderCrc = 0;
		if (!reader.ReadUInt16(decoded.flags)
			|| !reader.ReadUInt32(decoded.assetCount)
			|| !reader.ReadUInt64(decoded.directoryOffset)
			|| !reader.ReadUInt64(decoded.archiveSize)
			|| !reader.ReadUInt32(decoded.directoryCrc)
			|| !reader.ReadUInt32(storedHeaderCrc))
		{
			return Rewind(reader, start, ArchiveError::TruncatedHeader);
		}

		// 4. Checksum of everything read before the headerCrc field.
		const std::uint32_t actualHeaderCrc = ComputeCrc32(reader.GetBuffer().subspan(start, kHeaderCrcCoveredSize));
		if (actualHeaderCrc != storedHeaderCrc)
		{
			return Rewind(reader, start, ArchiveError::HeaderChecksumMismatch);
		}

		header = decoded;
		return ArchiveError::None;
	}

	// --- Directory entries -------------------------------------------------------------------

	void WriteEntry(ByteWriter& writer, const AssetEntry& entry)
	{
		writer.WriteUInt64(entry.id);
		writer.WriteUInt32(static_cast<std::uint32_t>(entry.type));
		writer.WriteUInt64(entry.offset);
		writer.WriteUInt64(entry.size);
		writer.WriteUInt32(entry.crc32);
	}

	ArchiveResult ReadEntry(ByteReader& reader, AssetEntry& entry) noexcept
	{
		// Checking the size up front means the field reads below cannot fail half-way.
		if (!reader.CanRead(kEntrySize))
		{
			return ArchiveError::TruncatedDirectory;
		}

		AssetEntry decoded;
		std::uint32_t type = 0;
		if (!reader.ReadUInt64(decoded.id)
			|| !reader.ReadUInt32(type)
			|| !reader.ReadUInt64(decoded.offset)
			|| !reader.ReadUInt64(decoded.size)
			|| !reader.ReadUInt32(decoded.crc32))
		{
			return ArchiveError::TruncatedDirectory; // unreachable, kept for safety
		}

		// Any uint32 value is representable by AssetType (its underlying type is uint32).
		// Whether the value is a *known* type is checked by the ArchiveReader, not here.
		decoded.type = static_cast<AssetType>(type);
		entry = decoded;
		return ArchiveError::None;
	}
}
