#pragma once

#include <cstdint>
#include <string>
#include <vector>

#include "Assets/AssetTypeInfo.h"
#include "Serialization/SerializationAdapter.h"
#include "Serialization/Versioning.h"

/*
* General-purpose demonstration type #1 (unrelated to physics): a player save profile.
* Uses strings, a sequence of structures, a sequence of strings and schema versioning.
*
* Everything about this type lives in this header: the structures, their adapters and their
* registration as an asset. Neither the serialization core nor the archive core was modified.
*
* Persistent references: an inventory item refers to its item definition through a stable
* `itemId` (a number from the game's item database), never through a pointer or an index.
*
* Schema history
*   V1: displayName, level, experience, inventory.
*   V2: + title (default "Novice"), + achievements (default empty).
*
* Encoding (V2):
*   | version: uint16 | displayName: string | level: uint32 | experience: uint64 |
*   | inventory: uint32 count, then per item: itemId uint32, name string, quantity uint16, equipped bool |
*   | title: string | achievements: uint32 count, then strings |
*   string = uint32 byte length + UTF-8 bytes.
*/
namespace asset::demo
{
    struct InventoryItem
    {
        std::uint32_t itemId = 0;
        std::string name;
        std::uint16_t quantity = 1;
        bool equipped = false;

        [[nodiscard]] friend bool operator==(const InventoryItem&, const InventoryItem&) = default;
    };

    struct PlayerProfile
    {
        // --- V1 ---
        std::string displayName;
        std::uint32_t level = 1;
        std::uint64_t experience = 0;
        std::vector<InventoryItem> inventory;

        // --- V2 --- (default values are used when reading V1 data)
        std::string title = "Novice";
        std::vector<std::string> achievements;

        [[nodiscard]] friend bool operator==(const PlayerProfile&, const PlayerProfile&) = default;
    };
}

namespace asset
{
    template<>
    struct SerializationTraits<demo::InventoryItem> : StructAdapter<demo::InventoryItem>
    {
        static constexpr auto Fields = MakeFields(
            &demo::InventoryItem::itemId,
            &demo::InventoryItem::name,
            &demo::InventoryItem::quantity,
            &demo::InventoryItem::equipped);
    };

    template<>
    struct SerializationTraits<demo::PlayerProfile> : VersionedStructAdapter<demo::PlayerProfile>
    {
        static constexpr SchemaVersion CurrentVersion = 2;
        static constexpr SchemaVersion MinSupportedVersion = 1;

        static constexpr auto Fields = MakeFields(
            &demo::PlayerProfile::displayName,
            &demo::PlayerProfile::level,
            &demo::PlayerProfile::experience,
            &demo::PlayerProfile::inventory,
            Field(&demo::PlayerProfile::title, 2),
            Field(&demo::PlayerProfile::achievements, 2));
    };

    template<>
    struct AssetTypeInfo<demo::PlayerProfile>
    {
        static constexpr AssetType Category = AssetType::Binary;
        static constexpr std::uint32_t Tag = MakeTypeTag("PROF");
    };
}
