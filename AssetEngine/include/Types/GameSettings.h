#pragma once

#include <array>
#include <cstdint>
#include <string>
#include <vector>

#include "Assets/AssetTypeInfo.h"
#include "Serialization/SerializationAdapter.h"

/*
* General-purpose demonstration type #2 (unrelated to physics): the game's options.
* A plain composite structure (no schema version) with an enum, a string, a fixed-size array and
* a sequence of structures that contain strings.
*
* Encoding:
*   | language: string | windowMode: uint8 | resolution: 2 x uint16 | vsync: bool |
*   | volumes: 3 x float (master, music, effects) |
*   | keyBindings: uint32 count, then per binding: action string, primaryKey uint16, secondaryKey uint16 |
*/
namespace asset::demo
{
    enum class WindowMode : std::uint8_t
    {
        Windowed = 0,
        Borderless = 1,
        Fullscreen = 2,
    };

    struct KeyBinding
    {
        std::string action;
        std::uint16_t primaryKey = 0;
        std::uint16_t secondaryKey = 0;

        [[nodiscard]] friend bool operator==(const KeyBinding&, const KeyBinding&) = default;
    };

    struct GameSettings
    {
        std::string language = "en-US";
        WindowMode windowMode = WindowMode::Windowed;
        std::array<std::uint16_t, 2> resolution{ 1280, 720 };
        bool vsync = true;
        std::array<float, 3> volumes{ 1.0f, 0.8f, 0.8f };
        std::vector<KeyBinding> keyBindings;

        [[nodiscard]] friend bool operator==(const GameSettings&, const GameSettings&) = default;
    };
}

namespace asset
{
    template<>
    struct SerializationTraits<demo::WindowMode> : EnumAdapter<demo::WindowMode, demo::WindowMode::Fullscreen>
    {
    };

    template<>
    struct SerializationTraits<demo::KeyBinding> : StructAdapter<demo::KeyBinding>
    {
        static constexpr auto Fields = MakeFields(
            &demo::KeyBinding::action,
            &demo::KeyBinding::primaryKey,
            &demo::KeyBinding::secondaryKey);
    };

    template<>
    struct SerializationTraits<demo::GameSettings> : StructAdapter<demo::GameSettings>
    {
        static constexpr auto Fields = MakeFields(
            &demo::GameSettings::language,
            &demo::GameSettings::windowMode,
            &demo::GameSettings::resolution,
            &demo::GameSettings::vsync,
            &demo::GameSettings::volumes,
            &demo::GameSettings::keyBindings);
    };

    template<>
    struct AssetTypeInfo<demo::GameSettings>
    {
        static constexpr AssetType Category = AssetType::Binary;
        static constexpr std::uint32_t Tag = MakeTypeTag("GSET");
    };
}
