
#pragma once

#include "../SerializationAdapter.h"

/*
* Demonstration type #1: a plain composite structure.
*
* Shows that a new type is added without touching the core: the structure and its adapter live
* in this header, and the core (Serializer, Deserializer, SerializationTraits.h) is unchanged.
*
* Encoding: x, y, z as IEEE-754 floats, little-endian. 12 bytes, no header.
*/
namespace asset::demo
{
    struct Vector3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        [[nodiscard]] friend bool operator==(const Vector3&, const Vector3&) = default;
    };
}

namespace asset
{
    template<>
    struct SerializationTraits<demo::Vector3> : StructAdapter<demo::Vector3>
    {
        static constexpr auto Fields = MakeFields(
            &demo::Vector3::x,
            &demo::Vector3::y,
            &demo::Vector3::z);
    };
}
