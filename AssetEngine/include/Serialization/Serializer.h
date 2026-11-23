
#pragma once

#include <cstddef>
#include <span>
#include <vector>

#include "ByteAccess/ByteWriter.h"
#include "Serialization/SerializationTraits.h"

namespace asset
{
    class Serializer
    {
    public:
        // Constructs an empty serializer.
        Serializer() noexcept = default;

        // Reserves memory for the internal buffer.
        explicit Serializer(std::size_t reserveBytes)
            : m_writer(reserveBytes)
        {
        }

        // Serializes a value using its corresponding traits.
        template<typename T>
        void Write(const T& value)
        {
            static_assert(Serializable<T>,
                "asset::Serializer::Write: this type has no SerializationTraits specialization. "
                "Specialize asset::SerializationTraits<T> (see SerializationAdapter.h).");

            SerializationTraits<T>::Serialize(m_writer, value);
        }

        // Returns the serialized data.
        [[nodiscard]] std::span<const std::byte> GetBuffer() const noexcept
        {
            return m_writer.GetBuffer();
        }

        // Returns the current buffer size.
        [[nodiscard]] std::size_t GetSize() const noexcept
        {
            return m_writer.GetSize();
        }

        // Removes all serialized data.
        void Clear() noexcept
        {
            m_writer.Clear();
        }

        // Transfers ownership of the serialized data.
        [[nodiscard]] std::vector<std::byte> ReleaseBuffer() noexcept
        {
            return m_writer.ReleaseBuffer();
        }

    private:
        ByteWriter m_writer;
    };
}
