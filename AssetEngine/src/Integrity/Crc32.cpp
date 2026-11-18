#include "Integrity/Crc32.h"

#include <array>

namespace asset
{
	namespace
	{
		/** @brief Reflected form of the CRC-32 polynomial 0x04C11DB7. */
		constexpr std::uint32_t kReflectedPolynomial = 0xEDB88320u;

		/**
		 * @brief Builds the lookup table: entry i is the CRC state change produced by the byte i.
		 *
		 * Evaluated at compile time, so the table is stored as constant data in the binary.
		 */
		constexpr std::array<std::uint32_t, 256> MakeTable() noexcept
		{
			std::array<std::uint32_t, 256> table{};
			for (std::uint32_t i = 0; i < 256; ++i)
			{
				std::uint32_t value = i;
				for (int bit = 0; bit < 8; ++bit)
				{
					// Shift out the low bit; when it is 1, the polynomial is "subtracted" (XOR).
					value = (value & 1u) != 0 ? (value >> 1) ^ kReflectedPolynomial : value >> 1;
				}
				table[i] = value;
			}
			return table;
		}

		constexpr std::array<std::uint32_t, 256> kTable = MakeTable();

		// Spot checks of the generated table against the published reference values.
		static_assert(kTable[0] == 0x00000000u);
		static_assert(kTable[1] == 0x77073096u);
		static_assert(kTable[255] == 0x2D02EF8Du);
	}

	void Crc32::Update(std::span<const std::byte> data) noexcept
	{
		std::uint32_t state = m_state;
		for (const std::byte byte : data)
		{
			const auto index = static_cast<std::uint8_t>(state ^ std::to_integer<std::uint32_t>(byte));
			state = (state >> 8) ^ kTable[index];
		}
		m_state = state;
	}

	std::uint32_t Crc32::GetValue() const noexcept
	{
		return m_state ^ 0xFFFFFFFFu;
	}

	void Crc32::Reset() noexcept
	{
		m_state = kInitialState;
	}

	std::uint32_t ComputeCrc32(std::span<const std::byte> data) noexcept
	{
		Crc32 crc;
		crc.Update(data);
		return crc.GetValue();
	}
}
