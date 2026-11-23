#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <span>
#include <stdexcept>
#include <string>
#include <type_traits>
#include <utility>
#include <vector>

#include "Serialization/SerializationTraits.h"
#include "Serialization/Utf8.h"

/*
* Core encodings of strings, variable-length sequences and fixed-size arrays.
*
*   std::string       | byte length: uint32 | UTF-8 bytes |      no terminating zero
*   std::vector<T>    | element count: uint32 | elements |      each element with its own traits
*   std::array<T, N>  | N elements |                            no prefix, N is part of the schema
*
* All lengths are little-endian uint32 (as every integer of the format).
*
* Reading rules (same strong guarantee as ByteReader: on failure the value is untouched and the
* read position goes back to where it was):
*   - A length is checked against the bytes that are really left *before* anything is allocated:
*     a string needs `length` bytes, a sequence at least `count x MinEncodedSize<T>()` bytes.
*     A corrupted length is reported as ReadError::LengthTooLarge without reserving memory.
*   - Hard limits (kMaxStringBytes, kMaxSequenceCount) cap what a single value can ask for, even
*     for element types whose minimum encoded size is 0.
*   - Strings are validated as UTF-8 before the std::string is created (ReadError::InvalidUtf8).
*
* Writing rules: the writer refuses to produce data the reader would reject. A string that is not
* valid UTF-8 throws std::invalid_argument, a value over a hard limit throws std::length_error.
* Both are programming errors on the caller's side, not data errors.
*/
namespace asset
{
    /** @brief Largest string accepted by the format, in bytes (16 MiB). */
    inline constexpr std::uint32_t kMaxStringBytes = 16u * 1024u * 1024u;

    /** @brief Largest element count of one sequence (16 Mi elements). */
    inline constexpr std::uint32_t kMaxSequenceCount = 16u * 1024u * 1024u;

    template<>
    struct SerializationTraits<std::string>
    {
        static void Serialize(ByteWriter& writer, const std::string& value)
        {
            if (value.size() > kMaxStringBytes)
            {
                throw std::length_error("asset: string is longer than kMaxStringBytes");
            }
            if (!IsValidUtf8(value))
            {
                throw std::invalid_argument("asset: string is not valid UTF-8");
            }

            writer.WriteUInt32(static_cast<std::uint32_t>(value.size()));
            writer.WriteBytes(std::as_bytes(std::span<const char>(value.data(), value.size())));
        }

        [[nodiscard]] static ReadResult Deserialize(ByteReader& reader, std::string& value)
        {
            const std::size_t initialPosition = reader.GetPosition();

            std::uint32_t length = 0;
            if (const ReadResult result = reader.ReadUInt32(length); !result)
            {
                return result;
            }

            if (length > kMaxStringBytes || length > reader.GetRemaining())
            {
                (void)reader.Seek(initialPosition);
                return ReadError::LengthTooLarge;
            }

            // Zero-copy view of the bytes: validated before the string is allocated.
            std::span<const std::byte> bytes;
            if (const ReadResult result = reader.ReadBytesView(length, bytes); !result)
            {
                (void)reader.Seek(initialPosition);
                return result;
            }

            if (!IsValidUtf8(bytes))
            {
                (void)reader.Seek(initialPosition);
                return ReadError::InvalidUtf8;
            }

            value.assign(reinterpret_cast<const char*>(bytes.data()), bytes.size());
            return ReadError::None;
        }

        [[nodiscard]] static constexpr std::size_t MinEncodedSize() noexcept
        {
            return sizeof(std::uint32_t);
        }
    };

    template<typename T>
        requires Serializable<T>
    struct SerializationTraits<std::vector<T>>
    {
        static void Serialize(ByteWriter& writer, const std::vector<T>& values)
        {
            if (values.size() > kMaxSequenceCount)
            {
                throw std::length_error("asset: sequence has more than kMaxSequenceCount elements");
            }

            writer.WriteUInt32(static_cast<std::uint32_t>(values.size()));
            for (const auto& element : values)
            {
                SerializationTraits<T>::Serialize(writer, element);
            }
        }

        [[nodiscard]] static ReadResult Deserialize(ByteReader& reader, std::vector<T>& values)
        {
            static_assert(std::is_default_constructible_v<T>,
                "std::vector<T> serialization needs a default-constructible element type.");

            const std::size_t initialPosition = reader.GetPosition();

            std::uint32_t count = 0;
            if (const ReadResult result = reader.ReadUInt32(count); !result)
            {
                return result;
            }

            constexpr std::size_t minElementSize = detail::MinEncodedSize<T>();
            const std::size_t remaining = reader.GetRemaining();
            const bool fitsInData = minElementSize == 0 || count <= remaining / minElementSize;
            if (count > kMaxSequenceCount || !fitsInData)
            {
                (void)reader.Seek(initialPosition);
                return ReadError::LengthTooLarge;
            }

            // The reservation is bounded by the data that is left (see the check above), so a
            // corrupted count cannot make the reader allocate more than the input justifies.
            std::vector<T> temporary;
            temporary.reserve(minElementSize > 0 ? count : std::min<std::size_t>(count, remaining));

            for (std::uint32_t i = 0; i < count; ++i)
            {
                T element{};
                if (const ReadResult result = SerializationTraits<T>::Deserialize(reader, element); !result)
                {
                    (void)reader.Seek(initialPosition);
                    return result;
                }
                temporary.push_back(std::move(element));
            }

            values = std::move(temporary);
            return ReadError::None;
        }

        [[nodiscard]] static constexpr std::size_t MinEncodedSize() noexcept
        {
            return sizeof(std::uint32_t);
        }
    };

    template<typename T, std::size_t N>
        requires Serializable<T>
    struct SerializationTraits<std::array<T, N>>
    {
        static void Serialize(ByteWriter& writer, const std::array<T, N>& values)
        {
            for (const T& element : values)
            {
                SerializationTraits<T>::Serialize(writer, element);
            }
        }

        [[nodiscard]] static ReadResult Deserialize(ByteReader& reader, std::array<T, N>& values)
        {
            const std::size_t initialPosition = reader.GetPosition();

            std::array<T, N> temporary{};
            for (T& element : temporary)
            {
                if (const ReadResult result = SerializationTraits<T>::Deserialize(reader, element); !result)
                {
                    (void)reader.Seek(initialPosition);
                    return result;
                }
            }

            values = std::move(temporary);
            return ReadError::None;
        }

        [[nodiscard]] static constexpr std::size_t MinEncodedSize() noexcept
        {
            return N * detail::MinEncodedSize<T>();
        }
    };
}
