
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "ByteAccess/ByteReader.h"
#include "Serialization/SerializationTraits.h"

namespace asset
{
    class Deserializer
    {
    public:
        // Constructs a deserializer over an existing byte buffer.
        explicit Deserializer(std::span<const std::byte> buffer) noexcept
            : m_reader(buffer)
        {
        }

        // Prevents construction from a temporary vector.
        explicit Deserializer(std::vector<std::byte>&& temporaryBuffer) = delete;

        // Deserializes a value using its corresponding traits.
        template<typename T>
        [[nodiscard]] ReadResult Read(T& value)
        {
            const std::size_t initialPosition = m_reader.GetPosition();

            const ReadResult result =
                SerializationTraits<T>::Deserialize(m_reader, value);

            // Restores the reader position if deserialization fails.
            if (!result)
            {
                (void)m_reader.Seek(initialPosition);
            }

            return result;
        }

        // Returns the current reading position.
        [[nodiscard]] std::size_t GetPosition() const noexcept
        {
            return m_reader.GetPosition();
        }

        // Returns the total buffer size.
        [[nodiscard]] std::size_t GetSize() const noexcept
        {
            return m_reader.GetSize();
        }

        // Returns the number of remaining bytes.
        [[nodiscard]] std::size_t GetRemaining() const noexcept
        {
            return m_reader.GetRemaining();
        }

        // Returns true if the entire buffer has been consumed.
        [[nodiscard]] bool IsAtEnd() const noexcept
        {
            return m_reader.IsAtEnd();
        }

        // Returns the underlying byte buffer.
        [[nodiscard]] std::span<const std::byte> GetBuffer() const noexcept
        {
            return m_reader.GetBuffer();
        }

    private:
        ByteReader m_reader;
    };
}
