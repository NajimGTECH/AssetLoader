#pragma once
#include <cstdint>
#include <string_view>

/**
 * @file ReadResult.h
 * @brief Result type returned by every ByteReader operation.
 *
 * Error handling strategy of the Byte Access Layer
 * ------------------------------------------------
 *  - Reading a truncated or corrupted buffer is an *expected* situation for an asset loader,
 *    not an exceptional one. Errors are therefore reported through a return value, never through
 *    exceptions. Every ByteReader method is `noexcept`.
 *  - `std::expected` is C++23, so ByteReader uses the "out parameter + status" pattern instead:
 *    the decoded value is written to a reference argument and the method returns a ReadResult.
 *  - ReadResult is marked `[[nodiscard]]`: ignoring the result of a read is a compiler warning
 *    (C4834 on MSVC), because it is almost always a bug.
 *  - ReadResult is contextually convertible to `bool` (true = success), so the common case stays
 *    short: `if (!reader.ReadUInt32(value)) { return ...; }`.
 *  - On failure, the reason is available with GetError() and can be turned into a diagnostic
 *    message with ToString().
 *  - Strong guarantee: a failed read never modifies the output argument and never moves the
 *    read position. The reader can still be used after a failure.
 */
namespace asset
{
	/**
	 * @brief Reason why a ByteReader operation failed.
	 */
	enum class ReadError : std::uint8_t
	{
		/** @brief The operation succeeded. */
		None = 0,

		/** @brief Not enough bytes remain in the buffer (or the requested position is past its end). */
		EndOfBuffer,

		/** @brief A boolean byte was neither 0x00 nor 0x01. */
		InvalidBool,

		/** @brief A decoded value is outside the range allowed by its type (e.g. unknown enum value). */
		InvalidValue,

		/** @brief A versioned schema was written with a version this build cannot read. */
		UnsupportedVersion,
	};

	/**
	 * @brief Returns a short, human-readable description of a ReadError, for diagnostics.
	 *
	 * @code
	 * std::cerr << "Read failed: " << asset::ToString(result.GetError()) << '\n';
	 * @endcode
	 */
	[[nodiscard]] constexpr std::string_view ToString(ReadError error) noexcept
	{
		switch (error)
		{
		case ReadError::None:        return "no error";
		case ReadError::EndOfBuffer: return "attempt to read past the end of the buffer";
		case ReadError::InvalidBool: return "invalid boolean value (expected 0 or 1)";
		case ReadError::InvalidValue: return "decoded value is out of the range allowed by its type";
		case ReadError::UnsupportedVersion: return "unsupported schema version";
		}
		return "unknown read error";
	}

	/**
	 * @class ReadResult
	 * @brief Status of a ByteReader operation: success, or the ReadError that caused the failure.
	 *
	 * The class is `[[nodiscard]]`, so every function returning it must have its result checked.
	 *
	 * @code
	 * std::uint32_t count = 0;
	 * if (const asset::ReadResult result = reader.ReadUInt32(count); !result)
	 * {
	 *     std::cerr << asset::ToString(result.GetError()) << '\n';
	 *     return false;
	 * }
	 * @endcode
	 */
	class [[nodiscard]] ReadResult
	{
	public:
		/** @brief Constructs a successful result. */
		constexpr ReadResult() noexcept = default;

		/**
		 * @brief Constructs a result from an error code (ReadError::None means success).
		 *
		 * Implicit on purpose, so that a result can be compared directly with an error code:
		 * `reader.ReadBool(flag) == asset::ReadError::InvalidBool`.
		 */
		constexpr ReadResult(ReadError error) noexcept : m_error(error) {}

		/** @brief Returns true if the operation succeeded. */
		[[nodiscard]] constexpr bool IsOk() const noexcept { return m_error == ReadError::None; }

		/** @brief Same as IsOk(), allows `if (reader.ReadX(value))`. */
		[[nodiscard]] constexpr explicit operator bool() const noexcept { return IsOk(); }

		/** @brief Returns the error code (ReadError::None on success). */
		[[nodiscard]] constexpr ReadError GetError() const noexcept { return m_error; }

		/** @brief Two results are equal if they carry the same error code. */
		[[nodiscard]] friend constexpr bool operator==(ReadResult, ReadResult) noexcept = default;

	private:
		ReadError m_error = ReadError::None;
	};
}
