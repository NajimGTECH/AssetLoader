#pragma once
#include <array>
#include <climits>
#include <concepts>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <span>

/**
 * @file LittleEndian.h
 * @brief Private helpers that convert unsigned integers to and from little-endian bytes.
 *
 * Not part of the public API (lives in src/, not include/).
 *
 * The conversion uses shifts and masks on the *value*, never a memcpy of the object, so the
 * result does not depend on the host byte order. MSVC and Clang turn these loops into a single
 * load/store on little-endian targets, so there is no runtime cost.
 */

// The encoding assumes 8-bit bytes and IEEE-754 floating-point types of the usual sizes.
static_assert(CHAR_BIT == 8, "The Byte Access Layer requires 8-bit bytes.");
static_assert(std::numeric_limits<float>::is_iec559 && sizeof(float) == 4, "float must be IEEE-754 binary32.");
static_assert(std::numeric_limits<double>::is_iec559 && sizeof(double) == 8, "double must be IEEE-754 binary64.");

namespace asset::detail
{
	/** @brief Splits `value` into sizeof(T) bytes, least significant byte first. */
	template <std::unsigned_integral T>
	[[nodiscard]] constexpr std::array<std::byte, sizeof(T)> StoreLittleEndian(T value) noexcept
	{
		std::array<std::byte, sizeof(T)> bytes{};
		for (std::size_t i = 0; i < sizeof(T); ++i)
		{
			bytes[i] = static_cast<std::byte>((value >> (8 * i)) & 0xFFu);
		}
		return bytes;
	}

	/** @brief Rebuilds a value from sizeof(T) bytes, least significant byte first. */
	template <std::unsigned_integral T>
	[[nodiscard]] constexpr T LoadLittleEndian(std::span<const std::byte, sizeof(T)> bytes) noexcept
	{
		T value = 0;
		for (std::size_t i = 0; i < sizeof(T); ++i)
		{
			value |= static_cast<T>(std::to_integer<T>(bytes[i]) << (8 * i));
		}
		return value;
	}
}
