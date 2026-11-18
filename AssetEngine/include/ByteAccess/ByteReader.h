#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

#include "ByteAccess/ReadResult.h"

/**
 * @file ByteReader.h
 * @brief Reads primitive values from an existing byte buffer, with bounds checking.
 *
 * The encoding is the one described in ByteWriter.h (little-endian, two's complement,
 * IEEE-754 bit patterns, booleans as a single 0x00 / 0x01 byte).
 *
 * Rules shared by every Read method
 * ---------------------------------
 *  - **Bounds checking**: before reading N bytes, the reader checks that N bytes remain.
 *    The check is written as `count <= size - position`, so it cannot overflow even for a huge
 *    `count` coming from a corrupted length field.
 *  - **Strong guarantee**: on failure, the output argument is not modified and the position does
 *    not move. On success, the position advances by exactly the number of bytes consumed.
 *  - **Validation**: ReadBool() rejects any byte other than 0x00 / 0x01 (ReadError::InvalidBool).
 *  - **No allocation, no exception**: every method is `noexcept` and never allocates.
 *  - **Non-owning**: the reader only holds a std::span. The buffer must outlive the reader and
 *    must not be modified or reallocated while the reader is in use.
 *
 * See ReadResult.h for the rationale of the error-handling approach.
 */
namespace asset
{
	/**
	 * @class ByteReader
	 * @brief Sequential, bounds-checked reader over a non-owning view of bytes.
	 *
	 * @code
	 * asset::ByteReader reader(writer.GetBuffer());
	 *
	 * std::uint32_t value = 0;
	 * float floatingValue = 0.0f;
	 * bool booleanValue = false;
	 *
	 * if (!reader.ReadUInt32(value) || !reader.ReadFloat(floatingValue) || !reader.ReadBool(booleanValue))
	 * {
	 *     // truncated or corrupted data: value / floatingValue / booleanValue keep their last valid content
	 * }
	 * @endcode
	 */
	class ByteReader
	{
	public:
		/** @brief Constructs a reader over an empty buffer. Every read fails with EndOfBuffer. */
		ByteReader() noexcept = default;

		/**
		 * @brief Constructs a reader over `buffer`, positioned at offset 0.
		 *
		 * Accepts a std::span, a `const std::vector<std::byte>&`, a std::array, or
		 * ByteWriter::GetBuffer(). The data is not copied.
		 */
		explicit ByteReader(std::span<const std::byte> buffer) noexcept;

		/**
		 * @brief Deleted: reading from a temporary vector would leave the reader with a dangling view.
		 *
		 * Store the vector in a variable first, then pass it to the reader.
		 */
		explicit ByteReader(std::vector<std::byte>&& temporaryBuffer) = delete;

		/** @name Unsigned integers (little-endian) */
		///@{
		ReadResult ReadUInt8(std::uint8_t& value) noexcept;
		ReadResult ReadUInt16(std::uint16_t& value) noexcept;
		ReadResult ReadUInt32(std::uint32_t& value) noexcept;
		ReadResult ReadUInt64(std::uint64_t& value) noexcept;
		///@}

		/** @name Signed integers (two's complement, little-endian) */
		///@{
		ReadResult ReadInt8(std::int8_t& value) noexcept;
		ReadResult ReadInt16(std::int16_t& value) noexcept;
		ReadResult ReadInt32(std::int32_t& value) noexcept;
		ReadResult ReadInt64(std::int64_t& value) noexcept;
		///@}

		/** @name Floating-point values (IEEE-754 bit pattern, little-endian) */
		///@{
		ReadResult ReadFloat(float& value) noexcept;
		ReadResult ReadDouble(double& value) noexcept;
		///@}

		/**
		 * @brief Reads one byte as a boolean.
		 * @return ReadError::InvalidBool if the byte is neither 0x00 nor 0x01 (position unchanged).
		 */
		ReadResult ReadBool(bool& value) noexcept;

		/**
		 * @brief Copies exactly `destination.size()` bytes into `destination`.
		 *
		 * An empty destination always succeeds and reads nothing. `destination` must not overlap
		 * the reader's buffer.
		 *
		 * @code
		 * std::array<std::byte, 4> magic{};
		 * if (!reader.ReadBytes(magic)) { ... }
		 * @endcode
		 */
		ReadResult ReadBytes(std::span<std::byte> destination) noexcept;

		/**
		 * @brief Zero-copy read: returns a view of the next `count` bytes of the buffer.
		 *
		 * The view points into the reader's buffer and has the same lifetime. Use it to forward a
		 * block (string data, nested asset...) without copying it, or to build a sub-reader:
		 * `asset::ByteReader sub(view);`
		 */
		ReadResult ReadBytesView(std::size_t count, std::span<const std::byte>& view) noexcept;

		/** @brief Advances the position by `count` bytes without reading them. */
		ReadResult Skip(std::size_t count) noexcept;

		/**
		 * @brief Moves the read position to an absolute offset.
		 * @return ReadError::EndOfBuffer if `position > GetSize()` (position unchanged).
		 *         `position == GetSize()` is valid and means "at the end".
		 */
		ReadResult Seek(std::size_t position) noexcept;

		/** @brief Current read offset, in bytes, from the start of the buffer. */
		[[nodiscard]] std::size_t GetPosition() const noexcept;

		/** @brief Total size of the buffer, in bytes. */
		[[nodiscard]] std::size_t GetSize() const noexcept;

		/** @brief Number of bytes left to read: GetSize() - GetPosition(). */
		[[nodiscard]] std::size_t GetRemaining() const noexcept;

		/** @brief True if at least `count` bytes remain (overflow-safe). */
		[[nodiscard]] bool CanRead(std::size_t count) const noexcept;

		/** @brief True if every byte has been consumed. */
		[[nodiscard]] bool IsAtEnd() const noexcept;

		/** @brief The whole buffer the reader was constructed with. */
		[[nodiscard]] std::span<const std::byte> GetBuffer() const noexcept;

	private:
		std::span<const std::byte> m_buffer;
		std::size_t m_position = 0;
	};
}
