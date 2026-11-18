#include "ByteAccess/ByteReader.h"

#include <algorithm>
#include <bit>

#include "LittleEndian.h"

namespace asset
{
	namespace
	{
		/**
		 * @brief Reads sizeof(T) bytes and decodes them as a little-endian unsigned integer.
		 *
		 * All fixed-size reads go through ReadBytesView(), so bounds checking and position
		 * handling live in a single place. `value` is only assigned after a successful read.
		 */
		template <std::unsigned_integral T>
		ReadResult ReadLittleEndian(ByteReader& reader, T& value) noexcept
		{
			std::span<const std::byte> bytes;
			const ReadResult result = reader.ReadBytesView(sizeof(T), bytes);
			if (result)
			{
				value = detail::LoadLittleEndian<T>(bytes.first<sizeof(T)>());
			}
			return result;
		}

		/** @brief Reads an unsigned integer of the same size, then converts it to `Target` (signed or float). */
		template <std::unsigned_integral Raw, typename Target, typename Convert>
		ReadResult ReadConverted(ByteReader& reader, Target& value, Convert convert) noexcept
		{
			Raw raw = 0;
			const ReadResult result = ReadLittleEndian(reader, raw);
			if (result)
			{
				value = convert(raw);
			}
			return result;
		}
	}

	ByteReader::ByteReader(std::span<const std::byte> buffer) noexcept
		: m_buffer(buffer)
	{
	}

	// --- Unsigned integers -------------------------------------------------------------------

	ReadResult ByteReader::ReadUInt8(std::uint8_t& value) noexcept
	{
		return ReadLittleEndian(*this, value);
	}

	ReadResult ByteReader::ReadUInt16(std::uint16_t& value) noexcept
	{
		return ReadLittleEndian(*this, value);
	}

	ReadResult ByteReader::ReadUInt32(std::uint32_t& value) noexcept
	{
		return ReadLittleEndian(*this, value);
	}

	ReadResult ByteReader::ReadUInt64(std::uint64_t& value) noexcept
	{
		return ReadLittleEndian(*this, value);
	}

	// --- Signed integers ---------------------------------------------------------------------
	// C++20 defines unsigned -> signed conversion as modulo 2^N (two's complement).

	ReadResult ByteReader::ReadInt8(std::int8_t& value) noexcept
	{
		return ReadConverted<std::uint8_t>(*this, value, [](std::uint8_t raw) { return static_cast<std::int8_t>(raw); });
	}

	ReadResult ByteReader::ReadInt16(std::int16_t& value) noexcept
	{
		return ReadConverted<std::uint16_t>(*this, value, [](std::uint16_t raw) { return static_cast<std::int16_t>(raw); });
	}

	ReadResult ByteReader::ReadInt32(std::int32_t& value) noexcept
	{
		return ReadConverted<std::uint32_t>(*this, value, [](std::uint32_t raw) { return static_cast<std::int32_t>(raw); });
	}

	ReadResult ByteReader::ReadInt64(std::int64_t& value) noexcept
	{
		return ReadConverted<std::uint64_t>(*this, value, [](std::uint64_t raw) { return static_cast<std::int64_t>(raw); });
	}

	// --- Floating point ----------------------------------------------------------------------

	ReadResult ByteReader::ReadFloat(float& value) noexcept
	{
		return ReadConverted<std::uint32_t>(*this, value, [](std::uint32_t raw) { return std::bit_cast<float>(raw); });
	}

	ReadResult ByteReader::ReadDouble(double& value) noexcept
	{
		return ReadConverted<std::uint64_t>(*this, value, [](std::uint64_t raw) { return std::bit_cast<double>(raw); });
	}

	// --- Boolean -----------------------------------------------------------------------------

	ReadResult ByteReader::ReadBool(bool& value) noexcept
	{
		if (!CanRead(1))
		{
			return ReadError::EndOfBuffer;
		}

		// Validate before consuming, so an invalid byte leaves the position untouched.
		const std::byte raw = m_buffer[m_position];
		if (raw != std::byte{ 0 } && raw != std::byte{ 1 })
		{
			return ReadError::InvalidBool;
		}

		value = (raw == std::byte{ 1 });
		++m_position;
		return ReadError::None;
	}

	// --- Raw bytes ---------------------------------------------------------------------------

	ReadResult ByteReader::ReadBytes(std::span<std::byte> destination) noexcept
	{
		std::span<const std::byte> source;
		const ReadResult result = ReadBytesView(destination.size(), source);
		if (result)
		{
			std::copy(source.begin(), source.end(), destination.begin());
		}
		return result;
	}

	ReadResult ByteReader::ReadBytesView(std::size_t count, std::span<const std::byte>& view) noexcept
	{
		if (!CanRead(count))
		{
			return ReadError::EndOfBuffer;
		}

		view = m_buffer.subspan(m_position, count);
		m_position += count;
		return ReadError::None;
	}

	ReadResult ByteReader::Skip(std::size_t count) noexcept
	{
		if (!CanRead(count))
		{
			return ReadError::EndOfBuffer;
		}

		m_position += count;
		return ReadError::None;
	}

	ReadResult ByteReader::Seek(std::size_t position) noexcept
	{
		if (position > m_buffer.size())
		{
			return ReadError::EndOfBuffer;
		}

		m_position = position;
		return ReadError::None;
	}

	// --- State -------------------------------------------------------------------------------

	std::size_t ByteReader::GetPosition() const noexcept
	{
		return m_position;
	}

	std::size_t ByteReader::GetSize() const noexcept
	{
		return m_buffer.size();
	}

	std::size_t ByteReader::GetRemaining() const noexcept
	{
		// Invariant: m_position <= m_buffer.size(), so this never underflows.
		return m_buffer.size() - m_position;
	}

	bool ByteReader::CanRead(std::size_t count) const noexcept
	{
		// Written as a subtraction instead of `m_position + count <= size` so that a huge `count`
		// (e.g. a corrupted length field) cannot overflow and wrap around.
		return count <= GetRemaining();
	}

	bool ByteReader::IsAtEnd() const noexcept
	{
		return m_position == m_buffer.size();
	}

	std::span<const std::byte> ByteReader::GetBuffer() const noexcept
	{
		return m_buffer;
	}
}
