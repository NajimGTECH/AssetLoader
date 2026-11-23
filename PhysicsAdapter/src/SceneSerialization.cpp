#include "PhysicsIO/SceneSerialization.h"

#include <stdexcept>
#include <utility>

#include "Serialization/ContainerTraits.h"

namespace physics_io
{
    namespace
    {
        using asset::ByteReader;
        using asset::ByteWriter;
        using asset::ReadError;
        using asset::ReadResult;
        using asset::SchemaVersion;
        using asset::SerializationTraits;

        template<typename T>
        void Write(ByteWriter& writer, const T& value)
        {
            SerializationTraits<T>::Serialize(writer, value);
        }

        template<typename T>
        [[nodiscard]] ReadResult Read(ByteReader& reader, T& value)
        {
            return SerializationTraits<T>::Deserialize(reader, value);
        }

        [[nodiscard]] constexpr bool IsSupported(SchemaVersion version) noexcept
        {
            return version >= kOldestSceneVersion && version <= kSceneVersion;
        }

        template<typename T>
        [[nodiscard]] constexpr std::size_t MinSize() noexcept
        {
            return asset::detail::MinEncodedSize<T>();
        }

        // Smallest encoded body of a version: used to bound the body count before allocating.
        [[nodiscard]] constexpr std::size_t MinBodySize(SchemaVersion version) noexcept
        {
            const std::size_t damping = version >= 2 ? 2 * sizeof(float) : 0;
            return MinSize<std::uint32_t>() + MinSize<MotionKind>() + 3 * MinSize<Float3>()
                + MinSize<Quaternion>() + sizeof(float) + MinSize<Float3>() + damping
                + MinSize<std::vector<ColliderRecord>>();
        }

        void WriteBody(ByteWriter& writer, const BodyRecord& body, SchemaVersion version)
        {
            Write(writer, body.id);
            Write(writer, body.motion);
            Write(writer, body.position);
            Write(writer, body.orientation);
            Write(writer, body.linearVelocity);
            Write(writer, body.angularVelocity);
            Write(writer, body.mass);
            Write(writer, body.inertia);
            if (version >= 2)
            {
                Write(writer, body.linearDamping);
                Write(writer, body.angularDamping);
            }
            Write(writer, body.colliders);
        }

        [[nodiscard]] ReadResult ReadBody(ByteReader& reader, BodyRecord& body, SchemaVersion version)
        {
            ReadResult result;
            (void)((result = Read(reader, body.id))
                && (result = Read(reader, body.motion))
                && (result = Read(reader, body.position))
                && (result = Read(reader, body.orientation))
                && (result = Read(reader, body.linearVelocity))
                && (result = Read(reader, body.angularVelocity))
                && (result = Read(reader, body.mass))
                && (result = Read(reader, body.inertia))
                && (version < 2 || ((result = Read(reader, body.linearDamping)) && (result = Read(reader, body.angularDamping))))
                && (result = Read(reader, body.colliders)));
            return result;
        }

        [[nodiscard]] ReadResult ReadBodies(ByteReader& reader, std::vector<BodyRecord>& bodies, SchemaVersion version)
        {
            std::uint32_t count = 0;
            if (const ReadResult result = reader.ReadUInt32(count); !result)
            {
                return result;
            }

            // Bounded before allocating: every body needs at least MinBodySize bytes.
            if (count > asset::kMaxSequenceCount || count > reader.GetRemaining() / MinBodySize(version))
            {
                return ReadError::LengthTooLarge;
            }

            bodies.clear();
            bodies.reserve(count);
            for (std::uint32_t i = 0; i < count; ++i)
            {
                BodyRecord body;
                if (const ReadResult result = ReadBody(reader, body, version); !result)
                {
                    return result;
                }
                bodies.push_back(std::move(body));
            }
            return ReadError::None;
        }
    }

    void WriteScene(ByteWriter& writer, const SceneRecord& scene, SchemaVersion version)
    {
        if (!IsSupported(version))
        {
            throw std::invalid_argument("physics_io::WriteScene: unsupported scene version");
        }
        if (scene.bodies.size() > asset::kMaxSequenceCount)
        {
            throw std::length_error("physics_io::WriteScene: too many bodies");
        }

        writer.WriteUInt16(version);
        Write(writer, scene.name);
        Write(writer, scene.gravity);
        Write(writer, scene.fixedTimestep);
        Write(writer, scene.stepIndex);
        Write(writer, scene.time);

        writer.WriteUInt32(static_cast<std::uint32_t>(scene.bodies.size()));
        for (const BodyRecord& body : scene.bodies)
        {
            WriteBody(writer, body, version);
        }

        Write(writer, scene.joints);
    }

    ReadResult ReadScene(ByteReader& reader, SceneRecord& scene)
    {
        const std::size_t initialPosition = reader.GetPosition();

        SchemaVersion version = 0;
        if (const ReadResult result = reader.ReadUInt16(version); !result)
        {
            return result;
        }

        ReadResult result = ReadError::None;
        if (!IsSupported(version))
        {
            result = ReadError::UnsupportedVersion;
        }

        SceneRecord temporary;
        if (result)
        {
            (void)((result = Read(reader, temporary.name))
                && (result = Read(reader, temporary.gravity))
                && (result = Read(reader, temporary.fixedTimestep))
                && (result = Read(reader, temporary.stepIndex))
                && (result = Read(reader, temporary.time))
                && (result = ReadBodies(reader, temporary.bodies, version))
                && (result = Read(reader, temporary.joints)));
        }

        if (!result)
        {
            (void)reader.Seek(initialPosition);
            return result;
        }

        scene = std::move(temporary);
        return ReadError::None;
    }
}
