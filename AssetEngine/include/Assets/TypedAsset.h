#pragma once

#include <cstddef>
#include <cstdint>
#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "Archive/ArchiveReader.h"
#include "Archive/ArchiveWriter.h"
#include "Assets/AssetTypeInfo.h"
#include "ByteAccess/ByteReader.h"
#include "ByteAccess/ByteWriter.h"
#include "Serialization/SerializationTraits.h"

/*
* Glue between the Serialization layer and the Archive layer.
*
* Neither core layer depends on the other: Serialization turns objects into bytes, Archive stores
* bytes under an AssetId. This header combines them for any type that has both an adapter
* (SerializationTraits<T>) and a registration (AssetTypeInfo<T>), without modifying either core.
*
* Payload of a typed asset:
*
*   | type tag: uint32 | value encoded by SerializationTraits<T> |
*
* Loading checks, in this order: the asset exists, its directory type is AssetTypeInfo<T>::Category,
* the payload starts with AssetTypeInfo<T>::Tag, the value decodes, and no byte is left over.
* On failure the output value is not modified.
*/
namespace asset
{
    template<typename T>
    concept StorableAsset = Serializable<T> && RegisteredAsset<T>;

    enum class AssetLoadError : std::uint8_t
    {
        None = 0,
        ArchiveFailure,   // the archive lookup failed (see GetArchiveError(): AssetNotFound, NotOpen)
        CategoryMismatch, // the directory entry has another AssetType
        TagMismatch,      // the payload belongs to another C++ type (or is not a typed asset)
        DecodeFailed,     // the value could not be decoded (see GetReadError())
        TrailingBytes,    // the value decoded but bytes are left after it
    };

    [[nodiscard]] constexpr std::string_view ToString(AssetLoadError error) noexcept
    {
        switch (error)
        {
        case AssetLoadError::None:             return "no error";
        case AssetLoadError::ArchiveFailure:   return "the asset could not be read from the archive";
        case AssetLoadError::CategoryMismatch: return "the asset has another type in the archive directory";
        case AssetLoadError::TagMismatch:      return "the asset payload belongs to another type";
        case AssetLoadError::DecodeFailed:     return "the asset payload could not be decoded";
        case AssetLoadError::TrailingBytes:    return "unexpected bytes after the asset payload";
        }
        return "unknown asset load error";
    }

    class [[nodiscard]] AssetLoadResult
    {
    public:
        constexpr AssetLoadResult() noexcept = default;
        constexpr AssetLoadResult(AssetLoadError error) noexcept : m_error(error) {}
        constexpr AssetLoadResult(ArchiveError archiveError) noexcept
            : m_error(archiveError == ArchiveError::None ? AssetLoadError::None : AssetLoadError::ArchiveFailure)
            , m_archiveError(archiveError)
        {
        }
        constexpr AssetLoadResult(ReadError readError) noexcept
            : m_error(readError == ReadError::None ? AssetLoadError::None : AssetLoadError::DecodeFailed)
            , m_readError(readError)
        {
        }

        [[nodiscard]] constexpr bool IsOk() const noexcept { return m_error == AssetLoadError::None; }
        [[nodiscard]] constexpr explicit operator bool() const noexcept { return IsOk(); }
        [[nodiscard]] constexpr AssetLoadError GetError() const noexcept { return m_error; }
        [[nodiscard]] constexpr ArchiveError GetArchiveError() const noexcept { return m_archiveError; }
        [[nodiscard]] constexpr ReadError GetReadError() const noexcept { return m_readError; }

        /** @brief Most precise diagnostic available (archive or decoding detail when there is one). */
        [[nodiscard]] constexpr std::string_view GetMessage() const noexcept
        {
            if (m_error == AssetLoadError::ArchiveFailure) { return ToString(m_archiveError); }
            if (m_error == AssetLoadError::DecodeFailed) { return ToString(m_readError); }
            return ToString(m_error);
        }

    private:
        AssetLoadError m_error = AssetLoadError::None;
        ArchiveError m_archiveError = ArchiveError::None;
        ReadError m_readError = ReadError::None;
    };

    /** @brief Encodes `value` as a typed asset payload (tag + value). */
    template<StorableAsset T>
    [[nodiscard]] std::vector<std::byte> EncodeTypedAsset(const T& value)
    {
        ByteWriter writer;
        writer.WriteUInt32(AssetTypeInfo<T>::Tag);
        SerializationTraits<T>::Serialize(writer, value);
        return writer.ReleaseBuffer();
    }

    /** @brief Decodes a typed asset payload. `value` is only assigned on success. */
    template<StorableAsset T>
    AssetLoadResult DecodeTypedAsset(std::span<const std::byte> payload, T& value)
    {
        ByteReader reader(payload);

        std::uint32_t tag = 0;
        if (!reader.ReadUInt32(tag) || tag != AssetTypeInfo<T>::Tag)
        {
            return AssetLoadError::TagMismatch;
        }

        T temporary{};
        if (const ReadResult result = SerializationTraits<T>::Deserialize(reader, temporary); !result)
        {
            return result.GetError();
        }

        if (!reader.IsAtEnd())
        {
            return AssetLoadError::TrailingBytes;
        }

        value = std::move(temporary);
        return AssetLoadError::None;
    }

    /** @brief Serializes `value` and adds it to the writer under `id` (no extra copy of the payload). */
    template<StorableAsset T>
    ArchiveResult AddTypedAsset(ArchiveWriter& writer, AssetId id, const T& value)
    {
        return writer.AddAsset(id, AssetTypeInfo<T>::Category, EncodeTypedAsset(value));
    }

    /**
     * @brief Loads one typed asset by ID. Only that asset's bytes are decoded: the other assets of
     * the archive are never deserialized.
     */
    template<StorableAsset T>
    AssetLoadResult LoadTypedAsset(const ArchiveReader& reader, AssetId id, T& value)
    {
        AssetEntry entry;
        if (const ArchiveResult result = reader.FindEntry(id, entry); !result)
        {
            return result.GetError();
        }

        if (entry.type != AssetTypeInfo<T>::Category)
        {
            return AssetLoadError::CategoryMismatch;
        }

        std::span<const std::byte> payload;
        if (const ArchiveResult result = reader.GetAssetData(id, payload); !result)
        {
            return result.GetError();
        }

        return DecodeTypedAsset(payload, value);
    }
}
