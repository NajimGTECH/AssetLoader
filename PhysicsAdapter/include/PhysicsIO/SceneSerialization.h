#pragma once

#include <cstddef>
#include <cstdint>

#include "Assets/AssetTypeInfo.h"
#include "PhysicsIO/SceneRecord.h"
#include "Serialization/SerializationAdapter.h"

/*
* Binary encoding of physics scenes, plugged into the serialization core through adapters.
* Nothing in AssetEngine was modified to support these types.
*
* Scene payload (little-endian, no padding):
*
*   | sceneVersion: uint16 | name: string | gravity: Float3 | fixedTimestep: float |
*   | stepIndex: uint64 | time: double | bodies: uint32 count + BodyRecord... |
*   | joints: uint32 count + JointRecord... |
*
*   Float3      = x, y, z floats (12 bytes)        Quaternion = w, x, y, z floats (16 bytes)
*   BodyRecord  = id uint32, motion uint8, position Float3, orientation Quaternion,
*                 linearVelocity Float3, angularVelocity Float3, mass float, inertia Float3,
*                 [version >= 2: linearDamping float, angularDamping float],
*                 colliders: uint32 count + ColliderRecord...
*   ColliderRecord = id uint32, shape uint8, radius float, halfExtents Float3, halfHeight float,
*                 localPosition Float3, localRotation Quaternion, friction float, restitution float
*   JointRecord = id uint32, bodyA uint32, bodyB uint32, anchorA Float3, anchorB Float3, restLength float
*
* One version number covers the whole scene document (one byte per body would be wasted with a
* version per record). Versions:
*   1: initial format.
*   2: adds linearDamping and angularDamping to each body. Reading version 1 gives them 0.
* Any other version is rejected with ReadError::UnsupportedVersion.
*/
namespace physics_io
{
    inline constexpr asset::SchemaVersion kSceneVersion = 2;
    inline constexpr asset::SchemaVersion kOldestSceneVersion = 1;

    /** @brief Writes `scene` in format `version` (kOldestSceneVersion..kSceneVersion), e.g. for test data. */
    void WriteScene(asset::ByteWriter& writer, const SceneRecord& scene, asset::SchemaVersion version);

    /** @brief Reads a scene of any supported version. Strong guarantee: `scene` untouched on failure. */
    [[nodiscard]] asset::ReadResult ReadScene(asset::ByteReader& reader, SceneRecord& scene);
}

namespace asset
{
    template<>
    struct SerializationTraits<physics_io::Float3> : StructAdapter<physics_io::Float3>
    {
        static constexpr auto Fields = MakeFields(&physics_io::Float3::x, &physics_io::Float3::y, &physics_io::Float3::z);
    };

    template<>
    struct SerializationTraits<physics_io::Quaternion> : StructAdapter<physics_io::Quaternion>
    {
        static constexpr auto Fields = MakeFields(
            &physics_io::Quaternion::w, &physics_io::Quaternion::x, &physics_io::Quaternion::y, &physics_io::Quaternion::z);
    };

    template<>
    struct SerializationTraits<physics_io::MotionKind> : EnumAdapter<physics_io::MotionKind, physics_io::MotionKind::Kinematic>
    {
    };

    template<>
    struct SerializationTraits<physics_io::ShapeKind> : EnumAdapter<physics_io::ShapeKind, physics_io::ShapeKind::Capsule>
    {
    };

    template<>
    struct SerializationTraits<physics_io::ColliderRecord> : StructAdapter<physics_io::ColliderRecord>
    {
        static constexpr auto Fields = MakeFields(
            &physics_io::ColliderRecord::id,
            &physics_io::ColliderRecord::shape,
            &physics_io::ColliderRecord::radius,
            &physics_io::ColliderRecord::halfExtents,
            &physics_io::ColliderRecord::halfHeight,
            &physics_io::ColliderRecord::localPosition,
            &physics_io::ColliderRecord::localRotation,
            &physics_io::ColliderRecord::friction,
            &physics_io::ColliderRecord::restitution);
    };

    template<>
    struct SerializationTraits<physics_io::JointRecord> : StructAdapter<physics_io::JointRecord>
    {
        static constexpr auto Fields = MakeFields(
            &physics_io::JointRecord::id,
            &physics_io::JointRecord::bodyA,
            &physics_io::JointRecord::bodyB,
            &physics_io::JointRecord::anchorA,
            &physics_io::JointRecord::anchorB,
            &physics_io::JointRecord::restLength);
    };

    // Hand-written adapter: the body layout depends on the version of the enclosing scene.
    template<>
    struct SerializationTraits<physics_io::SceneRecord>
    {
        static void Serialize(ByteWriter& writer, const physics_io::SceneRecord& scene)
        {
            physics_io::WriteScene(writer, scene, physics_io::kSceneVersion);
        }

        [[nodiscard]] static ReadResult Deserialize(ByteReader& reader, physics_io::SceneRecord& scene)
        {
            return physics_io::ReadScene(reader, scene);
        }

        // version + empty name + gravity + timestep + stepIndex + time + two empty sequences
        [[nodiscard]] static constexpr std::size_t MinEncodedSize() noexcept
        {
            return 2 + 4 + 12 + 4 + 8 + 8 + 4 + 4;
        }
    };

    template<>
    struct AssetTypeInfo<physics_io::SceneRecord>
    {
        static constexpr AssetType Category = AssetType::Scene;
        static constexpr std::uint32_t Tag = MakeTypeTag("PSCN");
    };
}
