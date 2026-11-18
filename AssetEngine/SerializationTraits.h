
#pragma once

#include <cstdint>
#include "include/ByteAccess/ByteReader.h"
#include "include/ByteAccess/ByteWriter.h"

namespace asset
{
    /*
    * Serialization traits
    * Each supported type defines how it is written and read.
    */
    template<typename T>
    struct SerializationTraits;

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
