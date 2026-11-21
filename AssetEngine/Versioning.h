
#pragma once

#include <cstddef>
#include <cstdint>

#include "SerializationAdapter.h"

/*
* Schema versioning.
*
* A versioned structure is written as:
*
*   | uint16 schema version | fields that exist in that version, in declaration order |
*
* Writing always uses the current version. Reading accepts every version in
* [MinSupportedVersion, CurrentVersion]:
*   - fields added after the stored version are not in the stream and keep the default value
*     given by the structure (default member initializers);
*   - an optional Migrate() hook then converts the old data to the current meaning;
*   - any other version (0, too old, or newer than this build) is rejected with
*     ReadError::UnsupportedVersion, and neither the value nor the read position change.
*
* Requirements on SerializationTraits<T> when deriving from VersionedStructAdapter<T>:
*
*   static constexpr SchemaVersion CurrentVersion = 2;
*   static constexpr SchemaVersion MinSupportedVersion = 1;
*   static constexpr auto Fields = MakeFields(&T::a, &T::b, Field(&T::c, 2));
*
*   // Optional: called after reading a version older than CurrentVersion.
*   static void Migrate(T& value, SchemaVersion fromVersion);
*
* Rules to evolve a schema safely:
*   - never remove, reorder or change the type of an existing field;
*   - add new fields with Field(&T::member, NewVersion) and give them a default value in T;
*   - increment CurrentVersion, and put the conversion logic (if any) in Migrate();
*   - raise MinSupportedVersion only when old data must stop being readable.
*/
namespace asset
{
    template<typename T>
    struct VersionedStructAdapter
    {
        static void Serialize(ByteWriter& writer, const T& value)
        {
            CheckSchema();

            using Traits = SerializationTraits<T>;
            writer.WriteUInt16(Traits::CurrentVersion);
            detail::WriteFields(writer, value, Traits::Fields, Traits::CurrentVersion);
        }

        [[nodiscard]] static ReadResult Deserialize(ByteReader& reader, T& value)
        {
            CheckSchema();

            using Traits = SerializationTraits<T>;
            const std::size_t initialPosition = reader.GetPosition();

            SchemaVersion version = 0;
            if (const ReadResult result = reader.ReadUInt16(version); !result)
            {
                return result;
            }

            if (!IsSupported(version))
            {
                (void)reader.Seek(initialPosition);
                return ReadError::UnsupportedVersion;
            }

            const ReadResult result = detail::ReadAtomically(reader, value, [&reader, version](T& temporary)
                {
                    const ReadResult fieldsResult = detail::ReadFields(reader, temporary, Traits::Fields, version);

                    if constexpr (requires(T & migrated, SchemaVersion fromVersion) { Traits::Migrate(migrated, fromVersion); })
                    {
                        if (fieldsResult && version < Traits::CurrentVersion)
                        {
                            Traits::Migrate(temporary, version);
                        }
                    }

                    return fieldsResult;
                });

            // ReadAtomically restored the position after the header: go back before it too.
            if (!result)
            {
                (void)reader.Seek(initialPosition);
            }

            return result;
        }

        // True if this build can read data written with `version`.
        [[nodiscard]] static constexpr bool IsSupported(SchemaVersion version) noexcept
        {
            return version >= SerializationTraits<T>::MinSupportedVersion
                && version <= SerializationTraits<T>::CurrentVersion;
        }

    private:
        static constexpr void CheckSchema() noexcept
        {
            using Traits = SerializationTraits<T>;

            static_assert(Traits::CurrentVersion >= 1,
                "VersionedStructAdapter: CurrentVersion must be at least 1.");
            static_assert(Traits::MinSupportedVersion >= 1 && Traits::MinSupportedVersion <= Traits::CurrentVersion,
                "VersionedStructAdapter: MinSupportedVersion must be in [1, CurrentVersion].");
            static_assert(detail::MinSinceVersion(Traits::Fields) >= 1,
                "VersionedStructAdapter: field versions start at 1.");
            static_assert(detail::MaxSinceVersion(Traits::Fields) <= Traits::CurrentVersion,
                "VersionedStructAdapter: a field is declared in a version newer than CurrentVersion.");
        }
    };
}
