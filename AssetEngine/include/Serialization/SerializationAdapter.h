
#pragma once

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <tuple>
#include <type_traits>
#include <utility>

#include "Serialization/SerializationTraits.h"

/*
* Adapter system of the serialization layer.
*
* The core (Serializer, Deserializer, SerializationTraits for primitives) knows nothing about
* engine- or game-specific types. A new type is plugged in from its own header by specializing
* asset::SerializationTraits<T>, usually by deriving from one of these reusable adapters:
*
*   StructAdapter<T>           Composite structure, fields written in declaration order.
*   EnumAdapter<E, Last>       Enumeration stored as its underlying integer, validated on read.
*   VersionedStructAdapter<T>  Composite structure with a schema version (see Versioning.h).
*
* Example (in the header that declares Vector3, not in the core):
*
*   namespace asset
*   {
*       template<>
*       struct SerializationTraits<Vector3> : StructAdapter<Vector3>
*       {
*           static constexpr auto Fields = MakeFields(&Vector3::x, &Vector3::y, &Vector3::z);
*       };
*   }
*
* A completely custom encoding is also possible: specialize SerializationTraits<T> by hand and
* provide Serialize / Deserialize (see the Serializable concept in SerializationTraits.h).
*/
namespace asset
{
    // Version number of a serialized schema. Versions start at 1.
    using SchemaVersion = std::uint16_t;

    /*
    * Describes one serialized member of a structure.
    * sinceVersion is the first schema version that contains the field (1 = always present).
    */
    template<typename Class, typename Member>
    struct FieldDescriptor
    {
        using ClassType = Class;
        using MemberType = Member;

        Member Class::* pointer;
        SchemaVersion sinceVersion;
    };

    // Declares a field, optionally added in a later schema version: Field(&Body::friction, 2).
    template<typename Class, typename Member>
    [[nodiscard]] constexpr FieldDescriptor<Class, Member> Field(Member Class::* pointer, SchemaVersion sinceVersion = 1) noexcept
    {
        return { pointer, sinceVersion };
    }

    namespace detail
    {
        template<typename Class, typename Member>
        [[nodiscard]] constexpr FieldDescriptor<Class, Member> ToField(Member Class::* pointer) noexcept
        {
            return { pointer, 1 };
        }

        template<typename Class, typename Member>
        [[nodiscard]] constexpr FieldDescriptor<Class, Member> ToField(FieldDescriptor<Class, Member> field) noexcept
        {
            return field;
        }
    }

    /*
    * Builds the field list of an adapter. Accepts plain member pointers (field present since
    * version 1) and Field(...) descriptors, in any mix:
    *   MakeFields(&Body::id, &Body::mass, Field(&Body::friction, 2))
    */
    template<typename... Items>
    [[nodiscard]] constexpr auto MakeFields(Items... items) noexcept
    {
        return std::make_tuple(detail::ToField(items)...);
    }

    namespace detail
    {
        // Smallest and largest sinceVersion of a field list (1 for an empty list).
        template<typename Fields>
        [[nodiscard]] constexpr SchemaVersion MinSinceVersion(const Fields& fields) noexcept
        {
            return std::apply([](const auto&... field)
                {
                    SchemaVersion result = 1;
                    ((result = std::min(result, field.sinceVersion)), ...);
                    return result;
                }, fields);
        }

        template<typename Fields>
        [[nodiscard]] constexpr SchemaVersion MaxSinceVersion(const Fields& fields) noexcept
        {
            return std::apply([](const auto&... field)
                {
                    SchemaVersion result = 1;
                    ((result = std::max(result, field.sinceVersion)), ...);
                    return result;
                }, fields);
        }

        template<typename Class, typename Member>
        void WriteField(ByteWriter& writer, const Class& value, const FieldDescriptor<Class, Member>& field, SchemaVersion version)
        {
            static_assert(Serializable<Member>,
                "A field of this structure has no SerializationTraits specialization.");

            if (field.sinceVersion <= version)
            {
                SerializationTraits<Member>::Serialize(writer, value.*(field.pointer));
            }
        }

        template<typename Class, typename Member>
        [[nodiscard]] ReadResult ReadField(ByteReader& reader, Class& value, const FieldDescriptor<Class, Member>& field, SchemaVersion version)
        {
            static_assert(Serializable<Member>,
                "A field of this structure has no SerializationTraits specialization.");

            // A field added after `version` is not in the stream: it keeps its default value.
            if (field.sinceVersion > version)
            {
                return ReadError::None;
            }

            return SerializationTraits<Member>::Deserialize(reader, value.*(field.pointer));
        }

        // Writes, in declaration order, every field that exists in `version`.
        template<typename Class, typename Fields>
        void WriteFields(ByteWriter& writer, const Class& value, const Fields& fields, SchemaVersion version)
        {
            std::apply([&](const auto&... field)
                {
                    (WriteField(writer, value, field, version), ...);
                }, fields);
        }

        // Reads, in declaration order, every field that exists in `version`. Stops at the first failure.
        template<typename Class, typename Fields>
        [[nodiscard]] ReadResult ReadFields(ByteReader& reader, Class& value, const Fields& fields, SchemaVersion version)
        {
            ReadResult result;
            std::apply([&](const auto&... field)
                {
                    (void)((result = ReadField(reader, value, field, version)) && ...);
                }, fields);
            return result;
        }

        /*
        * Reads into a default-constructed temporary and only assigns `value` on success.
        * On failure, the reader goes back to where it started: same strong guarantee as ByteReader.
        */
        template<typename T, typename ReadFunction>
        [[nodiscard]] ReadResult ReadAtomically(ByteReader& reader, T& value, ReadFunction read)
        {
            static_assert(std::is_default_constructible_v<T>,
                "Structure adapters need a default-constructible type (missing fields take their default value).");

            const std::size_t initialPosition = reader.GetPosition();

            T temporary{};
            const ReadResult result = read(temporary);
            if (!result)
            {
                (void)reader.Seek(initialPosition);
                return result;
            }

            value = std::move(temporary);
            return ReadError::None;
        }
    }

    /*
    * Adapter for composite structures without versioning.
    * Requirements on SerializationTraits<T>:
    *   static constexpr auto Fields = MakeFields(&T::a, &T::b, ...);
    * Encoding: the fields one after the other, no header, no padding.
    * Fields that are themselves structures are handled through their own SerializationTraits.
    */
    template<typename T>
    struct StructAdapter
    {
        static void Serialize(ByteWriter& writer, const T& value)
        {
            CheckFields();
            detail::WriteFields(writer, value, SerializationTraits<T>::Fields, 1);
        }

        [[nodiscard]] static ReadResult Deserialize(ByteReader& reader, T& value)
        {
            CheckFields();
            return detail::ReadAtomically(reader, value, [&reader](T& temporary)
                {
                    return detail::ReadFields(reader, temporary, SerializationTraits<T>::Fields, 1);
                });
        }

    private:
        static constexpr void CheckFields() noexcept
        {
            static_assert(detail::MinSinceVersion(SerializationTraits<T>::Fields) == 1
                && detail::MaxSinceVersion(SerializationTraits<T>::Fields) == 1,
                "StructAdapter has no schema version: use VersionedStructAdapter (Versioning.h) "
                "for fields declared with Field(pointer, sinceVersion).");
        }
    };

    /*
    * Adapter for enumerations: stored as the underlying integer type.
    * Valid values are the contiguous range [0, LastValue]. Anything else is rejected on read with
    * ReadError::InvalidValue (position unchanged), so a corrupted file never produces an enum
    * value the program does not know.
    *
    *   template<> struct SerializationTraits<BodyType> : EnumAdapter<BodyType, BodyType::Kinematic> {};
    */
    template<typename E, E LastValue>
        requires std::is_enum_v<E>
    struct EnumAdapter
    {
        using Underlying = std::underlying_type_t<E>;

        static_assert(Serializable<Underlying>,
            "EnumAdapter: the underlying type of the enum must be a fixed-width integer (e.g. std::uint8_t).");
        static_assert(static_cast<Underlying>(LastValue) >= 0,
            "EnumAdapter: LastValue must not be negative.");

        static void Serialize(ByteWriter& writer, const E& value)
        {
            SerializationTraits<Underlying>::Serialize(writer, static_cast<Underlying>(value));
        }

        [[nodiscard]] static ReadResult Deserialize(ByteReader& reader, E& value)
        {
            const std::size_t initialPosition = reader.GetPosition();

            Underlying raw{};
            const ReadResult result = SerializationTraits<Underlying>::Deserialize(reader, raw);
            if (!result)
            {
                return result;
            }

            if (!IsValid(raw))
            {
                (void)reader.Seek(initialPosition);
                return ReadError::InvalidValue;
            }

            value = static_cast<E>(raw);
            return ReadError::None;
        }

        [[nodiscard]] static constexpr bool IsValid(Underlying raw) noexcept
        {
            if constexpr (std::is_signed_v<Underlying>)
            {
                if (raw < 0)
                {
                    return false;
                }
            }
            return raw <= static_cast<Underlying>(LastValue);
        }
    };
}
