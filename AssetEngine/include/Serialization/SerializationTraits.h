
#pragma once

#include <concepts>
#include <cstdint>
#include "ByteAccess/ByteReader.h"
#include "ByteAccess/ByteWriter.h"

namespace asset
{
    /*
    * Serialization traits
    * Each supported type defines how it is written and read.
    *
    * This is the extension point of the serialization system: to support a new type, specialize
    * SerializationTraits<T> (by hand, or by deriving from one of the adapters of
    * SerializationAdapter.h / Versioning.h). The core never needs to be modified.
    *
    * The primary template is intentionally empty: a type without a specialization has no
    * Serialize / Deserialize, so it does not satisfy the Serializable concept below.
    */
    template<typename T>
    struct SerializationTraits
    {
    };

    /*
    * A type is serializable when its SerializationTraits provide:
    *   static void       Serialize(ByteWriter&, const T&);
    *   static ReadResult Deserialize(ByteReader&, T&);
    */
    template<typename T>
    concept Serializable = requires(ByteWriter & writer, ByteReader & reader, const T & constValue, T & value)
    {
        { SerializationTraits<T>::Serialize(writer, constValue) } -> std::same_as<void>;
        { SerializationTraits<T>::Deserialize(reader, value) } -> std::same_as<ReadResult>;
    };

    // Unsigned integers
    template<>
    struct SerializationTraits<std::uint8_t>
    {
        static void Serialize(ByteWriter& writer, const std::uint8_t& value)
        {
            writer.WriteUInt8(value);
        }

        static ReadResult Deserialize(ByteReader& reader, std::uint8_t& value)
        {
            return reader.ReadUInt8(value);
        }
    };

    template<>
    struct SerializationTraits<std::uint16_t>
    {
        static void Serialize(ByteWriter& writer, const std::uint16_t& value)
        {
            writer.WriteUInt16(value);
        }

        static ReadResult Deserialize(ByteReader& reader, std::uint16_t& value)
        {
            return reader.ReadUInt16(value);
        }
    };

    template<>
    struct SerializationTraits<std::uint32_t>
    {
        static void Serialize(ByteWriter& writer, const std::uint32_t& value)
        {
            writer.WriteUInt32(value);
        }

        static ReadResult Deserialize(ByteReader& reader, std::uint32_t& value)
        {
            return reader.ReadUInt32(value);
        }
    };

    template<>
    struct SerializationTraits<std::uint64_t>
    {
        static void Serialize(ByteWriter& writer, const std::uint64_t& value)
        {
            writer.WriteUInt64(value);
        }

        static ReadResult Deserialize(ByteReader& reader, std::uint64_t& value)
        {
            return reader.ReadUInt64(value);
        }
    };

    // Signed integers
    template<>
    struct SerializationTraits<std::int8_t>
    {
        static void Serialize(ByteWriter& writer, const std::int8_t& value)
        {
            writer.WriteInt8(value);
        }

        static ReadResult Deserialize(ByteReader& reader, std::int8_t& value)
        {
            return reader.ReadInt8(value);
        }
    };

    template<>
    struct SerializationTraits<std::int16_t>
    {
        static void Serialize(ByteWriter& writer, const std::int16_t& value)
        {
            writer.WriteInt16(value);
        }

        static ReadResult Deserialize(ByteReader& reader, std::int16_t& value)
        {
            return reader.ReadInt16(value);
        }
    };

    template<>
    struct SerializationTraits<std::int32_t>
    {
        static void Serialize(ByteWriter& writer, const std::int32_t& value)
        {
            writer.WriteInt32(value);
        }

        static ReadResult Deserialize(ByteReader& reader, std::int32_t& value)
        {
            return reader.ReadInt32(value);
        }
    };

    template<>
    struct SerializationTraits<std::int64_t>
    {
        static void Serialize(ByteWriter& writer, const std::int64_t& value)
        {
            writer.WriteInt64(value);
        }

        static ReadResult Deserialize(ByteReader& reader, std::int64_t& value)
        {
            return reader.ReadInt64(value);
        }
    };

    // Floating-point values
    template<>
    struct SerializationTraits<float>
    {
        static void Serialize(ByteWriter& writer, const float& value)
        {
            writer.WriteFloat(value);
        }

        static ReadResult Deserialize(ByteReader& reader, float& value)
        {
            return reader.ReadFloat(value);
        }
    };

    template<>
    struct SerializationTraits<double>
    {
        static void Serialize(ByteWriter& writer, const double& value)
        {
            writer.WriteDouble(value);
        }

        static ReadResult Deserialize(ByteReader& reader, double& value)
        {
            return reader.ReadDouble(value);
        }
    };

    // Boolean
    template<>
    struct SerializationTraits<bool>
    {
        static void Serialize(ByteWriter& writer, const bool& value)
        {
            writer.WriteBool(value);
        }

        static ReadResult Deserialize(ByteReader& reader, bool& value)
        {
            return reader.ReadBool(value);
        }
    };
}
