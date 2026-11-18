#pragma once
#include <cstddef>
#include <cstdint>
#include <span>

/**
 * @file Crc32.h
 * @brief CRC-32 checksum, used to detect corrupted archives.
 *
 * Algorithm: the standard CRC-32 (ISO-HDLC / IEEE 802.3), the one used by zip, PNG and gzip:
 *  - polynomial 0x04C11DB7, processed in reflected form (0xEDB88320),
 *  - initial value 0xFFFFFFFF, final XOR 0xFFFFFFFF.
 * Check value: CRC-32 of the ASCII string "123456789" is 0xCBF43926.
 *
 * What it guarantees: any change of 1, 2 or 3 bits, any odd number of changed bits, and any burst
 * of changed bits up to 32 bits long is always detected. Other random corruptions are missed with
 * a probability of about 1 in 4 billion. CRC-32 detects accidental damage (disk or transfer
 * errors, truncated copies); it is not a protection against deliberate tampering.
 *
 * The implementation is table-driven (one lookup per byte), with the 256-entry table computed at
 * compile time.
 */
namespace asset
{
	/**
	 * @class Crc32
	 * @brief Incremental CRC-32: feed the data in several pieces, get the same result as in one go.
	 *
	 * @code
	 * asset::Crc32 crc;
	 * crc.Update(header);
	 * crc.Update(directory);
	 * const std::uint32_t checksum = crc.GetValue();
	 * @endcode
	 */
	class Crc32
	{
	public:
		/** @brief Starts a new checksum (same state as after Reset()). */
		constexpr Crc32() noexcept = default;

		/** @brief Adds `data` to the checksum. An empty span changes nothing. */
		void Update(std::span<const std::byte> data) noexcept;

		/** @brief CRC-32 of all the data passed to Update() since construction or the last Reset(). */
		[[nodiscard]] std::uint32_t GetValue() const noexcept;

		/** @brief Forgets all data, to compute a new checksum. */
		void Reset() noexcept;

	private:
		static constexpr std::uint32_t kInitialState = 0xFFFFFFFFu;
		std::uint32_t m_state = kInitialState;
	};

	/** @brief CRC-32 of `data` in one call. ComputeCrc32({}) == 0. */
	[[nodiscard]] std::uint32_t ComputeCrc32(std::span<const std::byte> data) noexcept;
}
