#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>

/*
* UTF-8 validation (RFC 3629 / Unicode "well-formed UTF-8 byte sequences" table).
*
* Accepted sequences:
*   00..7F
*   C2..DF  80..BF
*   E0      A0..BF  80..BF          (no overlong 3-byte forms)
*   E1..EC  80..BF  80..BF
*   ED      80..9F  80..BF          (no UTF-16 surrogates U+D800..U+DFFF)
*   EE..EF  80..BF  80..BF
*   F0      90..BF  80..BF  80..BF  (no overlong 4-byte forms)
*   F1..F3  80..BF  80..BF  80..BF
*   F4      80..8F  80..BF  80..BF  (nothing above U+10FFFF)
*
* Everything else (C0, C1, F5..FF, lone continuation bytes, truncated sequences) is rejected.
* U+0000 is a valid code point, so a string may contain zero bytes.
*/
namespace asset
{
    [[nodiscard]] constexpr bool IsValidUtf8(std::span<const std::byte> bytes) noexcept
    {
        const std::size_t size = bytes.size();
        std::size_t i = 0;

        const auto at = [&bytes](std::size_t index) noexcept
            {
                return std::to_integer<std::uint8_t>(bytes[index]);
            };
        const auto inRange = [](std::uint8_t value, std::uint8_t low, std::uint8_t high) noexcept
            {
                return value >= low && value <= high;
            };

        while (i < size)
        {
            const std::uint8_t lead = at(i);

            if (lead <= 0x7F)
            {
                ++i;
                continue;
            }

            std::size_t length = 0;
            std::uint8_t secondLow = 0x80;
            std::uint8_t secondHigh = 0xBF;

            if (inRange(lead, 0xC2, 0xDF)) { length = 2; }
            else if (lead == 0xE0) { length = 3; secondLow = 0xA0; }
            else if (inRange(lead, 0xE1, 0xEC) || inRange(lead, 0xEE, 0xEF)) { length = 3; }
            else if (lead == 0xED) { length = 3; secondHigh = 0x9F; }
            else if (lead == 0xF0) { length = 4; secondLow = 0x90; }
            else if (inRange(lead, 0xF1, 0xF3)) { length = 4; }
            else if (lead == 0xF4) { length = 4; secondHigh = 0x8F; }
            else { return false; }

            if (length > size - i)
            {
                return false; // truncated sequence
            }

            if (!inRange(at(i + 1), secondLow, secondHigh))
            {
                return false;
            }

            for (std::size_t k = 2; k < length; ++k)
            {
                if (!inRange(at(i + k), 0x80, 0xBF))
                {
                    return false;
                }
            }

            i += length;
        }

        return true;
    }

    [[nodiscard]] inline bool IsValidUtf8(std::string_view text) noexcept
    {
        return IsValidUtf8(std::as_bytes(std::span<const char>(text.data(), text.size())));
    }
}
