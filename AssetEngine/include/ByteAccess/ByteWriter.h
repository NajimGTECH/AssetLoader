#pragma once
#include <cstddef>
#include <cstdint>
#include <span>
#include <vector>

/**
 * @file ByteWriter.h
 * @brief Appends primitive values to an owned, growable byte buffer using a portable encoding.
 *
 * Binary encoding (shared with ByteReader)
 * ----------------------------------------
 *  | Type                       | Size     | Encoding                                         |
 *  |----------------------------|----------|--------------------------------------------------|
 *  | uint8 / int8               | 1 byte   | the byte itself (int8 as two's complement)       |
 *  | uint16 / int16             | 2 bytes  | little-endian, two's complement for signed       |
 *  | uint32 / int32             | 4 bytes  | little-endian, two's complement for signed       |
 *  | uint64 / int64             | 8 bytes  | little-endian, two's complement for signed       |
 *  | float                      | 4 bytes  | IEEE-754 binary32 bit pattern, little-endian     |
 *  | double                     | 8 bytes  | IEEE-754 binary64 bit pattern, little-endian     |
 *  | bool                       | 1 byte   | 0x00 = false, 0x01 = true                        |
 *  | raw bytes                  | N bytes  | copied as-is, no length prefix                   |
 *
 *  - Little-endian means the least significant byte comes first: WriteUInt32(0x11223344)
 *    produces the bytes 44 33 22 11.
 *  - Values are converted with explicit shifts, never by copying the in-memory representation of
 *    an object. The output is therefore identical on little- and big-endian hosts.
 *  - Floating-point values are converted to their integer bit pattern with std::bit_cast, so
 *    every value (including -0.0, infinities and NaN payloads) is preserved bit-for-bit.
 *  - The layer adds no padding, alignment, type tags or length prefixes: it is independent of any
 *    archive format. Lengths for strings and sequences are the Serialization System's job
 *    (e.g. WriteUInt32(size) followed by WriteBytes(data)).
 *
 * Error handling
 * --------------
 * Writing to memory cannot fail except when memory allocation fails. In that case the standard
 * std::bad_alloc (or std::length_error) exception propagates and the buffer is left unchanged
 * (strong guarantee of std::vector::insert at the end). All Write methods return void.
 */
namespace asset
{
	/**
	 * @class ByteWriter
	 * @brief Serializes primitive values into an owned `std::vector<std::byte>`.
	 *
	 * @code
	 * asset::ByteWriter writer;
	 * writer.WriteUInt32(42);
	 * writer.WriteFloat(3.14f);
	 * writer.WriteBool(true);
	 *
	 * asset::ByteReader reader(writer.GetBuffer()); // 9 bytes: 2A 00 00 00 | C3 F5 48 40 | 01
	 * @endcode
	 */
	class ByteWriter
	{
	public:
		/** @brief Constructs an empty writer. Does not allocate. */
		ByteWriter() noexcept = default;

		/**
		 * @brief Constructs an empty writer and reserves capacity for `reserveBytes` bytes.
		 *
		 * Use it when the final size is known or can be estimated, to avoid reallocations.
		 */
		explicit ByteWriter(std::size_t reserveBytes);

		/** @name Unsigned integers (little-endian) */
		///@{
		void WriteUInt8(std::uint8_t value);
		void WriteUInt16(std::uint16_t value);
		void WriteUInt32(std::uint32_t value);
		void WriteUInt64(std::uint64_t value);
		///@}

		/** @name Signed integers (two's complement, little-endian) */
		///@{
		void WriteInt8(std::int8_t value);
		void WriteInt16(std::int16_t value);
		void WriteInt32(std::int32_t value);
		void WriteInt64(std::int64_t value);
		///@}

		/** @name Floating-point values (IEEE-754 bit pattern, little-endian) */
		///@{
		void WriteFloat(float value);
		void WriteDouble(double value);
		///@}

		/** @brief Writes one byte: 0x01 for true, 0x00 for false. */
		void WriteBool(bool value);

		/**
		 * @brief Appends a raw byte sequence as-is (no length prefix, no conversion).
		 *
		 * `bytes` may be empty, and may even be a view of this writer's own buffer
		 * (e.g. `writer.WriteBytes(writer.GetBuffer())` duplicates the content).
		 *
		 * @code
		 * const std::array<std::byte, 4> magic{ std::byte{'A'}, std::byte{'S'}, std::byte{'E'}, std::byte{'T'} };
		 * writer.WriteBytes(magic);
		 * @endcode
		 */
		void WriteBytes(std::span<const std::byte> bytes);

		/** @brief Reserves capacity for at least `totalBytes` bytes (does not change GetSize()). */
		void Reserve(std::size_t totalBytes);

		/** @brief Removes all written bytes. The capacity is kept, so the writer can be reused without reallocating. */
		void Clear() noexcept;

		/**
		 * @brief Returns a read-only view of the bytes written so far.
		 *
		 * The view does not own the data: it is invalidated by any later Write, Reserve, Clear or
		 * ReleaseBuffer call, and by the destruction of the writer.
		 */
		[[nodiscard]] std::span<const std::byte> GetBuffer() const noexcept;

		/** @brief Number of bytes written so far. */
		[[nodiscard]] std::size_t GetSize() const noexcept;

		/** @brief True if nothing has been written. */
		[[nodiscard]] bool IsEmpty() const noexcept;

		/**
		 * @brief Moves the buffer out of the writer, without copying. The writer becomes empty.
		 *
		 * Use it to hand the bytes over to another owner (archive builder, file writer...).
		 *
		 * @code
		 * std::vector<std::byte> bytes = writer.ReleaseBuffer(); // writer.GetSize() == 0
		 * @endcode
		 */
		[[nodiscard]] std::vector<std::byte> ReleaseBuffer() noexcept;

	private:
		std::vector<std::byte> m_buffer;
	};
}
