#include "ByteAccess/ByteWriter.h"

#include <algorithm>
#include <bit>
#include <functional>
#include <utility>

#include "LittleEndian.h"

namespace asset
{
	namespace
	{
		/** @brief Appends the little-endian encoding of an unsigned integer. */
		template <std::unsigned_integral T>
		void AppendLittleEndian(std::vector<std::byte>& buffer, T value)
		{
			const auto bytes = detail::StoreLittleEndian(value);
			buffer.insert(buffer.end(), bytes.begin(), bytes.end());
		}
	}

	ByteWriter::ByteWriter(std::size_t reserveBytes)
	{
		m_buffer.reserve(reserveBytes);
	}

	// --- Unsigned integers -------------------------------------------------------------------

	void ByteWriter::WriteUInt8(std::uint8_t value)
	{
		m_buffer.push_back(static_cast<std::byte>(value));
	}

	void ByteWriter::WriteUInt16(std::uint16_t value)
	{
		AppendLittleEndian(m_buffer, value);
	}

	void ByteWriter::WriteUInt32(std::uint32_t value)
	{
		AppendLittleEndian(m_buffer, value);
	}

	void ByteWriter::WriteUInt64(std::uint64_t value)
	{
		AppendLittleEndian(m_buffer, value);
	}

	// --- Signed integers ---------------------------------------------------------------------
	// C++20 guarantees two's complement and defines signed -> unsigned conversion as modulo 2^N,
	// so static_cast gives the two's complement bit pattern on every platform.

	void ByteWriter::WriteInt8(std::int8_t value)
	{
		WriteUInt8(static_cast<std::uint8_t>(value));
	}

	void ByteWriter::WriteInt16(std::int16_t value)
	{
		WriteUInt16(static_cast<std::uint16_t>(value));
	}

	void ByteWriter::WriteInt32(std::int32_t value)
	{
		WriteUInt32(static_cast<std::uint32_t>(value));
	}

	void ByteWriter::WriteInt64(std::int64_t value)
	{
		WriteUInt64(static_cast<std::uint64_t>(value));
	}

	// --- Floating point ----------------------------------------------------------------------

	void ByteWriter::WriteFloat(float value)
	{
		WriteUInt32(std::bit_cast<std::uint32_t>(value));
	}

	void ByteWriter::WriteDouble(double value)
	{
		WriteUInt64(std::bit_cast<std::uint64_t>(value));
	}

	// --- Boolean and raw bytes ---------------------------------------------------------------

	void ByteWriter::WriteBool(bool value)
	{
		WriteUInt8(value ? std::uint8_t{ 1 } : std::uint8_t{ 0 });
	}

	void ByteWriter::WriteBytes(std::span<const std::byte> bytes)
	{
		if (bytes.empty())
		{
			return;
		}

		// If `bytes` is a view of our own buffer, growing the vector would invalidate it.
		// In that case, remember the offset, grow first, then copy from inside the (new) storage.
		// std::less gives a total order on pointers, even for unrelated ones.
		const std::byte* const begin = m_buffer.data();
		const std::byte* const end = begin + m_buffer.size();
		const bool aliasesOwnBuffer = !std::less<const std::byte*>{}(bytes.data(), begin)
			&& std::less<const std::byte*>{}(bytes.data(), end);

		if (aliasesOwnBuffer)
		{
			const std::size_t sourceOffset = static_cast<std::size_t>(bytes.data() - begin);
			const std::size_t oldSize = m_buffer.size();
			m_buffer.resize(oldSize + bytes.size());
			// Source [sourceOffset, sourceOffset + n) lies before oldSize, destination starts at oldSize: no overlap.
			std::copy_n(m_buffer.begin() + static_cast<std::ptrdiff_t>(sourceOffset), bytes.size(),
				m_buffer.begin() + static_cast<std::ptrdiff_t>(oldSize));
			return;
		}

		m_buffer.insert(m_buffer.end(), bytes.begin(), bytes.end());
	}

	// --- Buffer management -------------------------------------------------------------------

	void ByteWriter::Reserve(std::size_t totalBytes)
	{
		m_buffer.reserve(totalBytes);
	}

	void ByteWriter::Clear() noexcept
	{
		m_buffer.clear();
	}

	std::span<const std::byte> ByteWriter::GetBuffer() const noexcept
	{
		return m_buffer;
	}

	std::size_t ByteWriter::GetSize() const noexcept
	{
		return m_buffer.size();
	}

	bool ByteWriter::IsEmpty() const noexcept
	{
		return m_buffer.empty();
	}

	std::vector<std::byte> ByteWriter::ReleaseBuffer() noexcept
	{
		// std::exchange guarantees the writer is left empty (a moved-from vector is only "valid but unspecified").
		return std::exchange(m_buffer, {});
	}
}
