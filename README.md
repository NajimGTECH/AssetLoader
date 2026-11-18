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
| `AssetLoader` | Console app    | Demo of the archive system (see [Demo](#demo)).              |
| `UnitTests`   | Native test DLL| Microsoft C++ Unit Test Framework tests for `AssetEngine`.   |

`AssetEngine` is split into layers, each in its own folder under `include/` and `src/`:

1. **ByteAccess**: low-level reading and writing of primitive values (`ByteWriter`, `ByteReader`).
2. **Archive**: archive header, asset directory, IDs, types, offsets (`ArchiveWriter`, `ArchiveReader`).
3. **Integrity**: CRC-32 checksums (`Crc32`), used by the Archive layer to detect corrupted or
   truncated archives.
4. **Serialization**: conversion of C++ types to and from bytes, built on ByteAccess. *(to do)*

Users include headers relative to `AssetEngine/include`, e.g. `#include "ByteAccess/ByteWriter.h"`.

## Demo

`AssetLoader` (set it as the startup project and press Ctrl+F5) walks through the archive system:

1. **Writing**: adds a text, a small 4x4 RGBA texture and a config file to an `ArchiveWriter`
   (a second asset with the same ID is refused), then saves the archive.
2. **Reading**: opens the file with `ArchiveReader` and prints the header and the asset directory
   (ID, type, offset, size, CRC-32).
3. **Loading by ID**: gets the text without a copy, loads the texture into a vector, and asks for
   an asset that is not in the archive.
4. **Damaged archives**: wrong magic number, one bit flipped in the header, in the directory and in
   an asset, a file cut short, an extra byte at the end, a missing file. Each one is refused with
   its own error.

Each step prints its result. The program returns 0 when every step gave the expected result.
The archive is written to `%TEMP%\AssetLoaderDemo.aset`; pass another path as the first argument
to write it somewhere else: `AssetLoader.exe my-archive.aset`.

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

## Archive Layer

Namespace `asset`, folder `Archive/`. Stores several assets in one binary file and loads any of
them by ID. Built on the Byte Access Layer, so every value is little-endian with no padding.

### File layout

| Section    | Offset                   | Size                      |
|------------|--------------------------|---------------------------|
| Header     | 0                        | 36 bytes                  |
| Directory  | `header.directoryOffset` | `assetCount` x 32 bytes   |
| Asset data | `entry.offset`           | `entry.size`, per asset   |

The writer puts the directory right after the header and the data right after the directory. The
reader only follows the stored offsets, so it does not depend on that order.

**Header** (36 bytes)

| Offset | Type    | Field             | Value                                         |
|--------|---------|-------------------|-----------------------------------------------|
| 0      | 4 bytes | `magic`           | `ASET` (`41 53 45 54`)                        |
| 4      | uint16  | `version`         | `kArchiveVersion` (2)                         |
| 6      | uint16  | `flags`           | 0 (reserved)                                  |
| 8      | uint32  | `assetCount`      | number of directory entries                   |
| 12     | uint64  | `directoryOffset` | absolute offset of the directory              |
| 20     | uint64  | `archiveSize`     | total size of the archive, in bytes           |
| 28     | uint32  | `directoryCrc`    | CRC-32 of the whole directory                 |
| 32     | uint32  | `headerCrc`       | CRC-32 of the 32 header bytes before it       |

**Directory entry** (32 bytes, sorted by strictly increasing ID)

| Offset | Type   | Field    | Value                                     |
|--------|--------|----------|-------------------------------------------|
| 0      | uint64 | `id`     | `AssetId` (never 0)                       |
| 8      | uint32 | `type`   | `AssetType`                               |
| 12     | uint64 | `offset` | absolute offset of the asset data         |
| 20     | uint64 | `size`   | size of the asset data (may be 0)         |
| 28     | uint32 | `crc32`  | CRC-32 of the asset data                  |

Version history: version 1 had no checksums (28-byte header and entries). Version 2 added
`directoryCrc`, `headerCrc` and the per-asset `crc32`. A version-1 file is rejected with
`UnsupportedVersion`.

Sorting the directory by ID lets the reader find an asset with a binary search, in O(log n).

### Asset IDs and types

- `asset::AssetId` is a `uint64`. It can be any non-zero number, or derived from a name with
  `asset::MakeAssetId("textures/player.png")` (64-bit FNV-1a, `constexpr`, same result on every
  platform). `0` (`kInvalidAssetId`) is reserved.
- `asset::AssetType` (`Binary`, `Text`, `Texture`, `Mesh`, `Audio`, `Material`, `Shader`, `Scene`)
  is stored as a `uint32`. Its numeric values are part of the format and must never change.

### API

```cpp
#include "Archive/ArchiveWriter.h"
#include "Archive/ArchiveReader.h"

// Saving several assets
asset::ArchiveWriter writer;
if (!writer.AddAsset(asset::MakeAssetId("player.png"), asset::AssetType::Texture, textureBytes)
    || !writer.AddAsset(asset::MakeAssetId("intro.txt"), asset::AssetType::Text, textBytes))
{
    // duplicate ID, ID 0 or invalid type
}
if (!writer.SaveToFile("game.asset")) { /* I/O error */ }

// Loading one asset by ID
asset::ArchiveReader archive;
if (const asset::ArchiveResult result = archive.OpenFile("game.asset"); !result)
{
    std::cerr << asset::ToString(result.GetError()) << '\n';
}

std::span<const std::byte> texture;   // zero-copy view into the archive
if (archive.GetAssetData(asset::MakeAssetId("player.png"), texture)) { /* ... */ }
```

**ArchiveWriter**

| Method | Description |
|--------|-------------|
| `AddAsset(id, type, span)` / `AddAsset(id, type, vector&&)` | Adds an asset (copied, or moved without copy). Rejects ID 0, invalid types and duplicate IDs. |
| `Contains(id)`, `GetAssetCount()`, `Clear()` | Manage the pending assets. |
| `Build()` | Encodes the whole archive into a `std::vector<std::byte>`. |
| `SaveToFile(path)` | `Build()` then writes the file (replacing any existing one). |

**ArchiveReader**

| Method | Description |
|--------|-------------|
| `Open(span)` | Opens a buffer owned by the caller (no copy, the buffer must outlive the reader). |
| `Open(vector&&)` | Takes ownership of the bytes (also chosen for a temporary vector, so it cannot dangle). |
| `OpenFile(path)` | Loads the file into memory, then opens it. |
| `GetAssetData(id, span&)` | Zero-copy view of an asset's bytes. |
| `LoadAsset(id, vector&)` | Copies an asset's bytes (when they must outlive the reader). |
| `FindEntry(id, entry&)`, `Contains(id)` | Look up the directory. |
| `GetHeader()`, `GetEntries()`, `GetAssetCount()`, `IsOpen()`, `Close()` | State of the open archive. |

### Error handling

Same approach as the Byte Access Layer: every operation returns a `[[nodiscard]]`
`asset::ArchiveResult` (true = success); `GetError()` gives an `asset::ArchiveError` and
`asset::ToString(error)` a diagnostic message.

When an archive is opened, every byte of it is checked (see *Integrity* below). After a successful
open, the archive is known to be intact, and looking up an asset can only fail with `AssetNotFound`.

Strong guarantee: a failed `AddAsset` leaves the writer unchanged, a failed `Open` keeps the
previously open archive, and lookups never modify their output argument on failure.

## Integrity

Namespace `asset`, folder `Integrity/`.

### CRC-32

```cpp
#include "Integrity/Crc32.h"

std::uint32_t checksum = asset::ComputeCrc32(bytes);   // one call

asset::Crc32 crc;                                      // or piece by piece
crc.Update(firstPart);
crc.Update(secondPart);
checksum = crc.GetValue();
```

Standard CRC-32 (the one used by zip, PNG and gzip): reflected polynomial `0xEDB88320`, initial
value and final XOR `0xFFFFFFFF`. Check value: `ComputeCrc32("123456789") == 0xCBF43926`. The
256-entry lookup table is computed at compile time (`constexpr`).

CRC-32 always detects 1-, 2- and 3-bit errors, any odd number of flipped bits, and any burst of
errors up to 32 bits long; other random corruptions are missed with a probability of about 1 in
4 billion. It is meant for accidental damage (disk, copy or transfer errors), not for deliberate
tampering.

### What the archive checks

The writer stores a CRC-32 of the header, of the directory and of each asset. When an archive is
opened, the reader checks, in this order:

| Step | Check | Error |
|------|-------|-------|
| 1. Header    | magic number, version | `InvalidMagic`, `UnsupportedVersion` |
|              | complete header, header checksum | `TruncatedHeader`, `HeaderChecksumMismatch` |
|              | reserved flags are 0 | `InvalidHeader` |
| 2. Size      | data shorter than `archiveSize` (truncated file) | `TruncatedArchive` |
|              | data longer than `archiveSize` (extra bytes) | `ArchiveSizeMismatch` |
| 3. Directory | after the header and inside the data | `InvalidDirectory`, `TruncatedDirectory` |
|              | directory checksum | `DirectoryChecksumMismatch` |
| 4. Metadata  | ID is not 0, type is a known `AssetType` | `InvalidAssetId`, `InvalidAssetType` |
|              | IDs sorted and unique | `InvalidDirectory` |
|              | data inside the archive (overflow-safe, in 64 bits) | `AssetOutOfBounds` |
|              | data does not overlap the header, the directory or another asset | `OverlappingData` |
| 5. Data      | checksum of every asset | `AssetChecksumMismatch` |

- The header checksum is checked before `archiveSize` is used, so a corrupted size field is
  reported as corruption, and a correct size reliably tells a truncated file from a damaged one.
- The magic number and the version are checked before the header checksum, so a file of another
  format or another version is reported as such.
- The structural checks of steps 3 and 4 are kept even though the checksums passed: a checksum
  only proves the bytes were not changed after writing, not that the writer produced valid
  metadata (bug in another tool, hand-made file).
- Every asset is verified when the archive is opened (one CRC pass over the data, about the cost
  of reading it), so `GetAssetData` never returns corrupted bytes and stays `noexcept`.

## Tests

`UnitTests` covers the Byte Access Layer: exact little-endian encoding of each type, round-trips
(including limits, `-0.0`, infinities, NaN payloads), boolean validation, empty buffers, reading
past the end (no side effects), overflow-safe bounds checks, raw bytes, zero-copy views, position
tracking and consecutive writes/reads.

It also covers the Archive Layer: exact bytes of the header and of a directory entry, FNV-1a
reference values, metadata checks of the writer (ID 0, invalid type, duplicates), sorted and
contiguous layout, saving and loading files, loading 500 assets by ID, and rejection of damaged
archives (truncation at every possible length, bad magic or version, unsorted or duplicated IDs,
out-of-bounds or overflowing offsets and sizes).

And the Integrity layer: CRC-32 reference values, cross-check with a bit-by-bit implementation,
incremental computation, detection of every single-bit error; in archives, detection of any
corrupted byte (header, directory or data, in memory and on disk), truncated files and trailing
bytes, invalid metadata (reserved flags, ID 0, unknown types) and overlapping data.

- **Visual Studio**: *Test > Run All Tests* (Test Explorer).
- **Command line**, after building:
  ```
  vstest.console.exe x64\Debug\UnitTests.dll
  ```
  (x86 builds go to `Debug\UnitTests.dll` / `Release\UnitTests.dll`.)
