#pragma once

#include <concepts>
#include <cstdint>

#include "Archive/AssetId.h"

/*
* Registration of a C++ type as an archive asset.
*
* The archive directory only knows coarse categories (asset::AssetType: Binary, Scene, ...), and its
* list must not grow every time a game adds a type: that would mean modifying the archive core.
* Instead, each typed asset payload starts with a 4-byte *type tag* chosen by the type itself.
* A type becomes storable by specializing AssetTypeInfo in its own header:
*
*   template<>
*   struct asset::AssetTypeInfo<game::PlayerProfile>
*   {
*       static constexpr AssetType Category = AssetType::Binary;     // directory entry type
*       static constexpr std::uint32_t Tag = MakeTypeTag("PROF");   // first 4 bytes of the payload
*   };
*
* Tags are part of the persisted format: never change or reuse one.
*/
namespace asset
{
    template<typename T>
    struct AssetTypeInfo
    {
    };

    /** @brief Builds a type tag from 4 ASCII characters, stored little-endian ("PROF" -> 50 52 4F 46). */
    [[nodiscard]] consteval std::uint32_t MakeTypeTag(const char (&text)[5]) noexcept
    {
        return static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[0]))
            | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[1])) << 8)
            | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[2])) << 16)
            | (static_cast<std::uint32_t>(static_cast<std::uint8_t>(text[3])) << 24);
    }

    /** @brief True if T has an AssetTypeInfo specialization (category + tag). */
    template<typename T>
    concept RegisteredAsset = requires
    {
        { AssetTypeInfo<T>::Category } -> std::convertible_to<AssetType>;
        { AssetTypeInfo<T>::Tag } -> std::convertible_to<std::uint32_t>;
    };
}
