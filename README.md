# AssetLoader

Asset Engine - Binary Archives (GTech 3, 2026-2027).

## Build

Requirements: Visual Studio 2022 (toolset v143) or Visual Studio 2026 (toolset v145), with the
"Desktop development with C++" workload. The projects use `$(DefaultPlatformToolset)`, so they
build in either version without retargeting. Language standard: C++20.

- **Visual Studio**: open `AssetLoader.slnx`, choose a configuration (Debug/Release, x64/x86),
  then *Build > Build Solution*.
- **Command line** (Developer PowerShell):
  ```
  msbuild AssetLoader.slnx /p:Configuration=Debug /p:Platform=x64
  ```

## Solution layout

| Project       | Type           | Content                                                      |
|---------------|----------------|--------------------------------------------------------------|
| `AssetEngine` | Static library | The archiving library. Public headers in `include/`, implementation in `src/`. |
| `AssetLoader` | Console app    | Demo application.                                            |
| `UnitTests`   | Native test DLL| Microsoft C++ Unit Test Framework tests for `AssetEngine`.   |

`AssetEngine` is split into layers, each in its own folder under `include/` and `src/`:

1. **ByteAccess**: low-level reading and writing of primitive values (`ByteWriter`, `ByteReader`).
2. **Archive**: archive header, asset directory, IDs, offsets, checksums. *(to do)*
3. **Serialization**: conversion of C++ types to and from bytes, built on ByteAccess
   (`Serializer.h`, `Deserializer.h`, `SerializationTraits.h`, `SerializationAdapter.h`,
   `Versioning.h`; demonstration types in `Types/`).

Users include headers relative to `AssetEngine/include`, e.g. `#include "ByteAccess/ByteWriter.h"`.

## Byte Access Layer

Namespace `asset`. Independent of the archive format: it adds no header, padding, alignment,
type tag or length prefix.

### Encoding

| Type             | Size    | Encoding                                       |
|------------------|---------|------------------------------------------------|
| `uint8`/`int8`   | 1 byte  | the byte itself (signed: two's complement)     |
| `uint16`/`int16` | 2 bytes | little-endian (signed: two's complement)       |
| `uint32`/`int32` | 4 bytes | little-endian (signed: two's complement)       |
| `uint64`/`int64` | 8 bytes | little-endian (signed: two's complement)       |
| `float`          | 4 bytes | IEEE-754 binary32 bit pattern, little-endian   |
| `double`         | 8 bytes | IEEE-754 binary64 bit pattern, little-endian   |
| `bool`           | 1 byte  | `0x00` = false, `0x01` = true, anything else is rejected on read |
| raw bytes        | N bytes | copied as-is, no length prefix                 |

Example: `WriteUInt32(0x11223344)` produces `44 33 22 11`.

Values are encoded with explicit shifts (and `std::bit_cast` for floating-point values), never by
copying an object's memory. The output is the same on any host, and floats keep their exact
bits (including `-0.0`, infinities and NaN payloads).

### API

```cpp
#include "ByteAccess/ByteWriter.h"
#include "ByteAccess/ByteReader.h"

asset::ByteWriter writer;                // owns a std::vector<std::byte>
writer.WriteUInt32(42);
writer.WriteFloat(3.14f);
writer.WriteBool(true);

asset::ByteReader reader(writer.GetBuffer()); // non-owning std::span, no copy

std::uint32_t value = 0;
float floatingValue = 0.0f;
bool booleanValue = false;

if (!reader.ReadUInt32(value) || !reader.ReadFloat(floatingValue) || !reader.ReadBool(booleanValue))
{
    // truncated or corrupted data
}
```

**ByteWriter**

| Method | Description |
|--------|-------------|
| `WriteUInt8/16/32/64`, `WriteInt8/16/32/64` | Fixed-width integers. |
| `WriteFloat`, `WriteDouble` | IEEE-754 values. |
| `WriteBool` | One byte, 0 or 1. |
| `WriteBytes(std::span<const std::byte>)` | Raw bytes as-is. Safe even with a view of the writer's own buffer. |
| `GetBuffer()` | `std::span<const std::byte>` view of the bytes written so far (invalidated by later writes). |
| `GetSize()`, `IsEmpty()` | Number of bytes written. |
| `Reserve(n)`, `ByteWriter(n)` | Pre-allocate, to avoid reallocations when the size is known. |
| `Clear()` | Empties the writer and keeps its capacity. |
| `ReleaseBuffer()` | Moves the `std::vector<std::byte>` out, without a copy. |

**ByteReader**

| Method | Description |
|--------|-------------|
| `ByteReader(std::span<const std::byte>)` | Reads from an existing buffer (vector, array, span). Construction from a temporary vector is deleted, because the view would dangle. |
| `ReadUInt8/16/32/64`, `ReadInt8/16/32/64`, `ReadFloat`, `ReadDouble`, `ReadBool` | Read into the reference argument. |
| `ReadBytes(std::span<std::byte>)` | Copies exactly `destination.size()` bytes. |
| `ReadBytesView(count, view)` | Zero-copy: `view` points into the buffer. Useful for strings, blobs or a sub-reader. |
| `Skip(count)`, `Seek(position)` | Move the read position. |
| `GetPosition()`, `GetSize()`, `GetRemaining()`, `CanRead(n)`, `IsAtEnd()` | Position tracking. |

### Error handling

- Every read returns an `asset::ReadResult`, which is `[[nodiscard]]` and converts to `bool`
  (true = success). `GetError()` returns an `asset::ReadError` (`None`, `EndOfBuffer` or
  `InvalidBool`), and `asset::ToString(error)` gives a diagnostic message.
- **Strong guarantee**: a failed read does not modify the output argument and does not move the
  read position. The reader stays usable after a failure.
- Bounds checks are written as `count <= size - position`, so a huge length read from a
  corrupted file cannot overflow the check.
- The reader never throws and never allocates (`noexcept`). The writer only fails if memory
  allocation fails (`std::bad_alloc`).

Why this approach: in a loader, truncated or corrupted input is an expected case, not an
exceptional one. `std::expected` is C++23. `std::optional<T>` cannot tell *why* a read failed. A
status return value with `[[nodiscard]]` is explicit, cheap, works without exceptions, and the
compiler warns when a result is ignored.

## Serialization: extensibility and versioning

### Core vs. specific types

The core (`Serializer`, `Deserializer`, `SerializationTraits.h` for primitives,
`SerializationAdapter.h`, `Versioning.h`) never includes a game or engine type. Each specific type
lives in its own header (see `Types/`) together with its adapter, i.e. a specialization of
`asset::SerializationTraits<T>`. Adding a type never requires modifying the core.

The `asset::Serializable<T>` concept checks that a type has an adapter. `Serializer::Write` and
`Deserializer::Read` use it in a `static_assert`, so a missing adapter is a clear compile error.

### Adapters

| Adapter | Use | Encoding |
|---------|-----|----------|
| `StructAdapter<T>` | Composite structure | Fields in declaration order, no header |
| `EnumAdapter<E, Last>` | Enumeration with values `0..Last` | Underlying integer; other values rejected with `ReadError::InvalidValue` |
| `VersionedStructAdapter<T>` | Structure whose schema evolves | `uint16` schema version, then the fields of that version |
| hand-written traits | Any custom encoding | Provide `Serialize` / `Deserialize` yourself |

```cpp
// Types/Vector3.h
namespace asset
{
    template<>
    struct SerializationTraits<demo::Vector3> : StructAdapter<demo::Vector3>
    {
        static constexpr auto Fields = MakeFields(&demo::Vector3::x, &demo::Vector3::y, &demo::Vector3::z);
    };
}
```

Fields can themselves be structures, enums or any type with an adapter. Reading a structure has
the same strong guarantee as `ByteReader`: on failure, the value is not modified and the read
position goes back to where it was.

### Versioning

```cpp
// Types/RigidBodyDesc.h
template<>
struct SerializationTraits<demo::RigidBodyDesc> : VersionedStructAdapter<demo::RigidBodyDesc>
{
    static constexpr SchemaVersion CurrentVersion = 2;
    static constexpr SchemaVersion MinSupportedVersion = 1;

    static constexpr auto Fields = MakeFields(
        &demo::RigidBodyDesc::id, &demo::RigidBodyDesc::mass,
        &demo::RigidBodyDesc::position, &demo::RigidBodyDesc::velocity,
        Field(&demo::RigidBodyDesc::type, 2),          // added in V2
        Field(&demo::RigidBodyDesc::friction, 2),      // added in V2
        Field(&demo::RigidBodyDesc::restitution, 2));  // added in V2

    // Optional: runs after reading an older version.
    static void Migrate(demo::RigidBodyDesc& body, SchemaVersion fromVersion) noexcept;
};
```

- Writing always uses `CurrentVersion`.
- Reading a version in `[MinSupportedVersion, CurrentVersion]`: only the fields of that version
  are read, newer fields keep the default value of the structure (default member initializers),
  then `Migrate()` converts old data (V1 -> V2: a body with `mass == 0` becomes `BodyType::Static`).
- Any other version (0, too old, newer than this build) is rejected with
  `ReadError::UnsupportedVersion`, without modifying the value or moving the read position.
- Mistakes in a schema (field newer than `CurrentVersion`, version 0, `Field(..., 2)` in a
  non-versioned `StructAdapter`) are caught at compile time.

Rules to evolve a schema: never remove, reorder or change the type of an existing field; add new
fields with `Field(&T::member, NewVersion)` and a default value; increment `CurrentVersion`; put
conversion logic in `Migrate()`.

## Tests

`UnitTests` covers the Byte Access Layer: exact little-endian encoding of each type, round-trips
(including limits, `-0.0`, infinities, NaN payloads), boolean validation, empty buffers, reading
past the end (no side effects), overflow-safe bounds checks, raw bytes, zero-copy views, position
tracking and consecutive writes/reads.

`SerializationAdapterTests` and `VersioningTests` cover the adapters (structures, enums, a
hand-written adapter defined in the tests), the V1 -> V2 migration, default values and the
rejection of unsupported versions.

- **Visual Studio**: *Test > Run All Tests* (Test Explorer).
- **Command line**, after building:
  ```
  vstest.console.exe x64\Debug\UnitTests.dll
  ```
  (x86 builds go to `Debug\UnitTests.dll` / `Release\UnitTests.dll`.)
