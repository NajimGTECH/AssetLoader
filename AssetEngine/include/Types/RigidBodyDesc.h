
#pragma once

#include <cstdint>

#include "Serialization/SerializationAdapter.h"
#include "Serialization/Versioning.h"
#include "Types/Vector3.h"

/*
* Demonstration type #2: a versioned structure that nests another structure and an enum.
*
* Schema history
* --------------
*   V1: id, mass, position, velocity.
*       A body with mass == 0 was implicitly static.
*   V2: + type (BodyType), friction, restitution.
*       New fields take their default value when reading V1 data, and Migrate() turns the
*       "mass == 0 means static" convention of V1 into an explicit BodyType::Static.
*
* Encoding (V2): | version: uint16 | id: uint32 | mass: float | position: 3 floats |
*                | velocity: 3 floats | type: uint8 | friction: float | restitution: float |
*                = 2 + 4 + 4 + 12 + 12 + 1 + 4 + 4 = 43 bytes (34 bytes in V1).
*/
namespace asset::demo
{
    enum class BodyType : std::uint8_t
    {
        Static = 0,
        Dynamic = 1,
        Kinematic = 2,
    };

    struct RigidBodyDesc
    {
        // --- V1 ---
        std::uint32_t id = 0;
        float mass = 1.0f;
        Vector3 position{};
        Vector3 velocity{};

        // --- V2 --- (default values are used when reading V1 data)
        BodyType type = BodyType::Dynamic;
        float friction = 0.5f;
        float restitution = 0.0f;

        [[nodiscard]] friend bool operator==(const RigidBodyDesc&, const RigidBodyDesc&) = default;
    };
}

namespace asset
{
    template<>
    struct SerializationTraits<demo::BodyType> : EnumAdapter<demo::BodyType, demo::BodyType::Kinematic>
    {
    };

    template<>
    struct SerializationTraits<demo::RigidBodyDesc> : VersionedStructAdapter<demo::RigidBodyDesc>
    {
        static constexpr SchemaVersion CurrentVersion = 2;
        static constexpr SchemaVersion MinSupportedVersion = 1;

        static constexpr auto Fields = MakeFields(
            &demo::RigidBodyDesc::id,
            &demo::RigidBodyDesc::mass,
            &demo::RigidBodyDesc::position,
            &demo::RigidBodyDesc::velocity,
            Field(&demo::RigidBodyDesc::type, 2),
            Field(&demo::RigidBodyDesc::friction, 2),
            Field(&demo::RigidBodyDesc::restitution, 2));

        // Migration V1 -> V2: V1 had no BodyType, a null mass meant a static body.
        static void Migrate(demo::RigidBodyDesc& body, SchemaVersion fromVersion) noexcept
        {
            if (fromVersion < 2)
            {
                body.type = (body.mass == 0.0f) ? demo::BodyType::Static : demo::BodyType::Dynamic;
            }
        }
    };
}
