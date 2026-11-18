# AssetLoader

Asset Engine - Binary Archives (GTech 3, 2026-2027).

A reusable binary asset archiving library in C++20, with no external dependency. It stores several
assets in one file, loads any of them by ID, detects truncated or corrupted files with CRC-32
checksums, and still reads the previous version of its format.

**Contents**: [Build](#build) · [Solution layout](#solution-layout) · [Demo](#demo) ·
[Byte Access Layer](#byte-access-layer) · [Archive Layer](#archive-layer) ·
[Versions and compatibility](#versions-and-compatibility) · [Integrity](#integrity) ·
[Tests](#tests) · [Memory and performance](#memory-and-performance) ·
[Limitations](#limitations-and-known-defects) · [Sources](#sources) ·
[Use of generative AI](#use-of-generative-ai) · [Division of work](#division-of-work)

### Status against the project brief

| Brief requirement | Status | Where |
|-------------------|--------|-------|
| Standalone library and demo application, C++20 | Done | [Solution layout](#solution-layout), [Demo](#demo) |
| Byte access separated from the archive format | Done | [Byte Access Layer](#byte-access-layer) |
| Byte order, sizes and lengths specified, no raw memory dumps | Done | [Encoding](#encoding), [File layout](#file-layout) |
| Magic number, version, asset directory (ID, type, offset, size) | Done | [File layout](#file-layout) |
| Write several assets, list the entries, load one asset by ID | Done | [Archive Layer API](#api-1) |
| Two versions of the schema, previous one loaded with default values, unknown ones rejected | Done (archive format) | [Versions and compatibility](#versions-and-compatibility) |
| Checksum stored and verified, algorithm and covered bytes documented | Done | [Integrity](#integrity) |
| Validation: magic, versions, sizes, offsets, overflows, duplicates, types, bounded allocations, no partial state | Done | [What the archive checks](#what-the-archive-checks), [Copies and allocations](#copies-and-allocations) |
| Tests: round-trips, empty archive, access by ID, migration, unknown versions, truncation, altered byte, excessive size, write failure | Done | [Tests](#tests) |
| Measurements for two data volumes: size, times, temporary memory | Done | [Measurements](#measurements) |
| Archives for both versions, with commands to reproduce the round-trips | Done | [Sample archives](#sample-archives) |
| General-purpose serialization (strings, sequences, composite types, adapters) | In progress (Student 2) | branch `feature/Sérialisation` |
| Physics adapter, scene and state restoration | Not in the repository yet | — |

## Build

Requirements: Visual Studio 2022 (toolset v143) or Visual Studio 2026 (toolset v145), with the
"Desktop development with C++" workload. The projects use `$(DefaultPlatformToolset)`, so they
build in either version without retargeting. Language standard: C++20. No external library.

- **Visual Studio**: open `AssetLoader.slnx`, choose a configuration (Debug/Release, x64/x86),
  then *Build > Build Solution*. Run the tests with *Test > Run All Tests*.
- **Command line** (Developer PowerShell), build, test and run the demo:
  ```
  msbuild AssetLoader.slnx /p:Configuration=Release /p:Platform=x64
  vstest.console.exe x64\Release\UnitTests.dll
  x64\Release\AssetLoader.exe
  ```
  x64 builds go to `x64\Debug\` and `x64\Release\`, and x86 builds go to `Debug\` and `Release\`.

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
4. **Serialization**: conversion of C++ types to and from bytes, built on ByteAccess. *(in progress
   on branch `feature/Sérialisation`, not merged yet)*

Users include headers relative to `AssetEngine/include`, e.g. `#include "ByteAccess/ByteWriter.h"`.

## Demo

`AssetLoader` is a small command-line tool built on the library. In Visual Studio, set it as the
startup project and press Ctrl+F5 (no arguments = walk-through demo); command-line arguments go in
*Project Properties > Debugging > Command Arguments*. From a terminal, after building x64 Release:

| Command | What it does |
|---------|--------------|
| `x64\Release\AssetLoader.exe` | Walk-through demo (below). The archive goes to `%TEMP%\AssetLoaderDemo.aset`. |
| `x64\Release\AssetLoader.exe demo <archive>` | Same, with the archive written to `<archive>`. |
| `x64\Release\AssetLoader.exe samples Data` | Writes the same three assets as `Data\sample_v1.aset` (format version 1) and `Data\sample_v2.aset` (version 2). |
| `x64\Release\AssetLoader.exe inspect <archive>` | Opens an archive and prints its header and directory, or why it was rejected. |
| `x64\Release\AssetLoader.exe migrate <in> <out>` | Opens an archive of any supported version and saves it in the current version. |
| `x64\Release\AssetLoader.exe bench` | Measures sizes, times and temporary memory (see [Memory and performance](#memory-and-performance)). |

Every command returns 0 on success and 1 on failure, with a diagnostic message.

The walk-through demo runs these steps and prints the result of each one:

1. **Writing**: adds a text, a small 4x4 RGBA texture and a config file to an `ArchiveWriter`
   (a second asset with the same ID is refused), then saves the archive.
2. **Reading**: opens the file with `ArchiveReader` and prints the header and the asset directory
   (ID, type, offset, size, CRC-32).
3. **Loading by ID**: gets the text without a copy, loads the texture into a vector, and asks for
   an asset that is not in the archive.
4. **Damaged archives**: wrong magic number, one bit flipped in the header, in the directory and in
   an asset, a file cut short, an extra byte at the end, a missing file. Each one is refused with
   its own error.
5. **Format versions**: writes a version-1 archive, loads it with the current reader, migrates it
   to version 2, and shows that an unknown version is refused, on reading and on writing.

### Sample archives

`Data/sample_v1.aset` and `Data/sample_v2.aset` hold the same three assets in both format
versions. To reproduce them and check the round-trips:

```
x64\Release\AssetLoader.exe samples Data
x64\Release\AssetLoader.exe inspect Data\sample_v1.aset
x64\Release\AssetLoader.exe migrate Data\sample_v1.aset %TEMP%\migrated.aset
fc /b %TEMP%\migrated.aset Data\sample_v2.aset
```

The output is deterministic, so `samples` rewrites byte-identical files, and the migrated version-1
archive is byte-identical to `sample_v2.aset` (`fc` reports no differences).

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

Sorting the directory by ID lets the reader find an asset with a binary search, in O(log n).

### Versions and compatibility

| Version | Header | Entry | Difference |
|---------|--------|-------|------------|
| 1 | 28 bytes | 28 bytes | No checksums: the tables above without `directoryCrc`, `headerCrc` and `crc32`. |
| 2 (current) | 36 bytes | 32 bytes | Adds the three checksum fields, appended at the end of the header and of each entry. |

- **Reading**: `ArchiveReader` loads every version from `kOldestArchiveVersion` (1) to
  `kArchiveVersion` (2). The version is read right after the magic number and selects the header
  and entry layouts. When a version-1 archive is loaded, the fields added in version 2 get their
  **default value 0** (`header.directoryCrc`, `entry.crc32`), the checksum checks are skipped and
  `HasChecksums()` returns false. Every other check (sizes, offsets, IDs, types, overlaps) still
  applies.
- **Unknown versions** (0, 3 and above) are rejected with `UnsupportedVersion`, before the rest of
  the header is read, because their layout is unknown. `ArchiveWriter::SetFormatVersion` rejects
  them too.
- **Migration**: re-saving with a writer upgrades an archive to the current version, the checksums
  being computed on the way (`AssetLoader.exe migrate` does exactly this):
  ```cpp
  asset::ArchiveReader oldArchive;
  asset::ArchiveWriter upgraded;                      // writes kArchiveVersion by default
  if (oldArchive.OpenFile("old.aset") && upgraded.AddAssets(oldArchive))
  {
      upgraded.SaveToFile("new.aset");
  }
  ```
- **Writing an older version**: `writer.SetFormatVersion(1)` produces version-1 archives, for an
  older reader or as test data. It is never needed otherwise.
- **Strategy**: the version number covers the archive *container* (header and directory layout),
  not the content of the assets. When the layout changes, the version is increased, new fields are
  appended rather than inserted, and the reader keeps a decoding path for every older version it
  supports. Each asset's own schema (the encoding of its payload) is versioned separately by the
  Serialization layer.

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
| `AddAssets(reader)` | Copies every asset of an open archive (any version), all or nothing. Used for migration. |
| `SetFormatVersion(v)`, `GetFormatVersion()` | Format version to write (default: current). |
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
| `GetHeader()`, `GetEntries()`, `GetAssetCount()`, `IsOpen()`, `Close()` | State of the open archive. `GetEntries()` lists the directory. |
| `HasChecksums()` | True if the open archive's checksums were verified (false for version 1). |

### Error handling

Same approach as the Byte Access Layer: every operation returns a `[[nodiscard]]`
`asset::ArchiveResult` (true = success); `GetError()` gives an `asset::ArchiveError` and
`asset::ToString(error)` a diagnostic message.

When an archive is opened, every byte of it is checked (see *Integrity* below; version-1 archives
have no checksums, so only their structure is checked). After a successful
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
4 billion. It is meant for accidental damage (disk, copy or transfer errors).

**It does not guarantee authenticity.** CRC-32 uses no secret key: anyone who modifies an archive
can recompute the checksums, and the reader will accept it. Detecting deliberate tampering would
need a cryptographic signature or an HMAC, which is out of scope.

The CRC-32 is our own implementation: no checksum library is used. It is checked against the
published reference values (see [Sources](#sources)).

### Bytes covered (format version 2)

| Checksum | Stored at | Covers |
|----------|-----------|--------|
| `headerCrc` | header bytes 32-35 | header bytes 0-31: magic, version, flags, assetCount, directoryOffset, archiveSize, directoryCrc |
| `directoryCrc` | header bytes 28-31 | the whole directory: `assetCount` x 32 bytes from `directoryOffset` (every entry, its `crc32` included) |
| `crc32` of an entry | entry bytes 28-31 | the `size` data bytes of that asset, from its `offset` |

Every byte of an archive written by `ArchiveWriter` is covered by exactly one checksum, except
`headerCrc` itself. The metadata is covered too: a changed ID, type, offset or size changes the
directory checksum, and the stored `archiveSize` (protected by `headerCrc`) detects a file that is
cut short or has extra bytes. Version-1 archives have no checksums (see
[Versions and compatibility](#versions-and-compatibility)).

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

For a version-1 archive, steps 1 to 4 run without the three checksum checks.

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

And versioning: exact bytes of a version-1 header and entry, loading a version-1 archive with
default values for the added fields, same content in both versions, structural checks still applied
to version 1, migration to the current version (in memory and through files, byte-identical to an
archive written directly), downgrade then upgrade, all-or-nothing `AddAssets`, and rejection of
unknown versions (0, 3, 100, 256, 65535) on reading and on writing.

How the brief's required test cases map to the tests:

| Required case | Tests |
|---------------|-------|
| Round-trips | `RoundTripTests`, `ArchiveReaderTests::SaveThenOpenFile`, `ArchiveVersioningTests::MigrationThroughFiles` |
| Empty archive and empty asset | `ArchiveWriterTests::EmptyArchiveIsHeaderOnly`, `ArchiveReaderTests::EmptyArchive`, empty asset in every sample archive |
| Access by asset ID | `ArchiveReaderTests::ManyAssetsAreFoundByBinarySearch`, `UnknownIdIsNotFoundAndOutputsAreUntouched` |
| Migration | `ArchiveVersioningTests::ReaderLoadsVersion1Archive`, `AddedFieldsGetTheirDefaultValue`, `MigrationToTheCurrentVersion` |
| Unknown versions | `ArchiveVersioningTests::RejectsEveryUnknownVersion`, `ArchiveFormatTests::ReadHeaderRejectsUnsupportedVersion` |
| Truncation | `ArchiveReaderTests::RejectsEveryTruncation`, `ArchiveIntegrityTests::TruncatedFileOnDiskIsReported` |
| Altered byte | `ArchiveIntegrityTests::DetectsEverySingleByteCorruption`, `CorruptedFileOnDiskIsReported` |
| Excessive size | `ArchiveReaderTests::RejectsHugeAssetCount`, `RejectsHugeDirectoryOffset`, `RejectsOffsetPlusSizeOverflow`, `ByteReaderTests::HugeCountsDoNotOverflowTheBoundsCheck` |
| Write failure | `ArchiveWriterTests::SaveToFileReportsOpenFailure` |
| Invalid reference | Serialization layer (references between assets are resolved there). |

Result: **148 tests, all passing** on x64 Debug and x86 Release, built with `/W4` without warnings.

- **Visual Studio**: *Test > Run All Tests* (Test Explorer).
- **Command line**, after building:
  ```
  vstest.console.exe x64\Debug\UnitTests.dll
  ```
  (x86 builds go to `Debug\UnitTests.dll` / `Release\UnitTests.dll`.)

## Memory and performance

### Data structures

- **The archive in memory** is one contiguous `std::vector<std::byte>`: one allocation, read
  sequentially, cache-friendly. The reader keeps it and hands out `std::span` views, so
  `GetAssetData` copies nothing. With `Open(span)`, the reader does not even own the bytes (for
  example a buffer the caller already has).
- **The reader's directory** is a `std::vector<AssetEntry>` sorted by ID: lookups are a binary
  search (O(log n)) over contiguous memory, with no extra index. A hash map would cost one
  allocation per entry and bring no measurable gain at these sizes (1000 lookups take 11 µs, see
  below). The file stores the directory already sorted, so nothing is sorted on load.
- **The writer** keeps its assets in a `std::map<AssetId, PendingAsset>`: it keeps them sorted in
  directory order and detects duplicates in O(log n) per insertion. The `AddAsset(vector&&)`
  overload takes the bytes without copying them.
- **The file format is not the memory layout**: headers and entries are decoded field by field
  with `ByteReader` into plain structs. `AssetEntry` takes 40 bytes in memory (with padding) but
  32 bytes in the file, and no struct is ever copied to or from the file as raw memory.

### Copies and allocations

| Operation | Copies of the asset data | Heap allocations |
|-----------|--------------------------|------------------|
| `AddAsset(id, type, span)` | 1 (into the writer) | 2 per asset (data + map node) |
| `AddAsset(id, type, vector&&)` | 0 | 1 per asset (map node) |
| `Build()` | 1 (into the archive buffer) | 2 (directory + archive, the archive reserved at its exact size, so no reallocation) |
| `SaveToFile(path)` | `Build()`, then written | 2 |
| `OpenFile(path)` | 1 (file to buffer) | 3 (buffer, directory, overlap check) |
| `Open(span)` | 0 | 2 (directory, overlap check) |
| `GetAssetData(id, span&)` | 0 | 0 |
| `LoadAsset(id, vector&)` | 1 | at most 1 (reuses the vector's capacity) |

**Allocations are bounded before reading.** No size read from a file is used for an allocation
before it is checked against the real size of the data. The directory is reserved only after
`assetCount x entry size` has been checked to fit inside the data, so a corrupted count cannot
make the reader reserve gigabytes. Asset sizes are checked against the archive before any view or
copy. `OpenFile` allocates the real file size reported by the system, not the size stored in the
header. `ByteReader` never allocates.

### Measurements

Command: `x64\Release\AssetLoader.exe bench` (source: `AssetLoader/Benchmark.cpp`).

**Method**
- The two data volumes are reproducible. Each asset is filled by a `std::mt19937` seeded with its
  index, and lookups use an order shuffled with a fixed seed, so every run builds byte-identical
  archives.
- Each operation runs 5 times and the **median** time is reported (`std::chrono::steady_clock`).
- Memory is measured by replacing the global `operator new` / `operator delete` of the demo
  executable (`AssetLoader/MemoryTracker.cpp`) with versions that count the bytes currently
  allocated. *Peak memory* is the highest amount allocated at the same time during the
  operation, above what was allocated before it. *Kept after* is what the result of the operation
  still holds (the writer, the reader, the built buffer).
- Machine: AMD Ryzen 7 9800X3D, 32 GB RAM, SSD, Windows 11, MSVC v145, Release x64.

**Small: 100 assets x 1 KiB.** Archive: 105,636 bytes (103.2 KiB), of which 3,236 bytes of header
and directory (3.06 %).

| Operation                              |   Median time | Peak memory |  Kept after | Allocs |
|----------------------------------------|--------------:|------------:|------------:|-------:|
| Add assets to the writer (copy)        |      0.197 ms |   107.1 KiB |   107.1 KiB |    201 |
| Build the archive in memory            |      0.012 ms |   106.3 KiB |   103.2 KiB |      2 |
| Save to file (build + write)           |      0.487 ms |   106.3 KiB |         0 B |      2 |
| Open from memory (checks + CRC-32)     |      0.182 ms |     5.5 KiB |     3.9 KiB |      2 |
| Open file (read + checks + CRC-32)     |      0.270 ms |   108.7 KiB |   107.1 KiB |      3 |
| Get every asset by ID (zero-copy view) |      0.001 ms |         0 B |         0 B |      0 |
| Load every asset by ID (copy)          |      0.004 ms |     1.0 KiB |         0 B |    100 |

**Large: 1000 assets x 64 KiB.** Archive: 65,568,036 bytes (62.53 MiB), of which 32,036 bytes of
header and directory (0.05 %).

| Operation                              |   Median time | Peak memory |  Kept after | Allocs |
|----------------------------------------|--------------:|------------:|------------:|-------:|
| Add assets to the writer (copy)        |    122.283 ms |   62.61 MiB |   62.61 MiB |   2001 |
| Build the archive in memory            |      6.963 ms |   62.56 MiB |   62.53 MiB |      2 |
| Save to file (build + write)           |     39.015 ms |   62.56 MiB |         0 B |      2 |
| Open from memory (checks + CRC-32)     |    114.787 ms |    54.8 KiB |    39.1 KiB |      2 |
| Open file (read + checks + CRC-32)     |    151.673 ms |   62.58 MiB |   62.57 MiB |      3 |
| Get every asset by ID (zero-copy view) |      0.011 ms |         0 B |         0 B |      0 |
| Load every asset by ID (copy)          |      1.497 ms |    64.0 KiB |         0 B |   1000 |

**Analysis**
- **Size**: the format costs 36 bytes plus 32 bytes per asset. That is 3 % of the archive for
  1 KiB assets and 0.05 % for 64 KiB assets.
- **Memory when writing**: the writer holds a copy of every asset, and `Build` / `SaveToFile`
  allocate one more buffer of the archive's size. Saving therefore needs about **twice the
  payload** at its peak. Adding with `AddAsset(vector&&)` removes the first copy when the caller
  can give its buffers away.
- **Memory when reading**: `OpenFile` holds exactly one copy of the archive plus 40 bytes per
  entry. `Open(span)` on a buffer the caller already has costs only the directory (39 KiB for
  1000 assets). Loading an asset by ID then costs nothing more (zero-copy view), or one copy of
  that asset with `LoadAsset`.
- **Time**: CRC-32 dominates. `Add assets` (122 ms) is the CRC computed when each asset is added,
  plus the copy. `Open from memory` (115 ms) is almost entirely the CRC check of the data, about
  550 MB/s with the one-byte-per-step table. A plain copy of the same data takes about 7 ms
  (`Build`). A faster CRC (slicing-by-8, several bytes per step) would be the first optimisation.
- **Access by ID** is negligible: 1000 binary searches take 11 µs.

**Limitations of the measurements**
- They come from one machine, with synthetic random data. The absolute times depend on the CPU;
  only the ratios carry over to other machines.
- The file was written just before being read, so `Open file` reads from the system's file cache,
  not from the SSD. The times are for a warm cache. `Save` does not force the data to disk
  (no flush to the device), so it measures the copy into the cache.
- Only C++ heap allocations are counted. The file cache, stack memory and the allocator's own
  overhead per block are not.
- The Small times are a few microseconds. That is close to the timer resolution and to system
  noise, so they vary between runs; for example `Save` varied from 0.2 to 0.5 ms.

## Limitations and known defects

- **The whole archive is loaded into memory**: there is no streaming and no memory mapping.
  `OpenFile` reads and verifies every byte, even to load a single asset, so opening costs time
  proportional to the archive size. An archive larger than the available memory, or than the
  address space of an x86 build, cannot be opened.
- **The writer keeps every asset in memory** until it saves, and saving needs about twice the
  payload (see above).
- **Asset IDs are 64-bit FNV-1a hashes of names.** Two different names may produce the same ID;
  the probability is about n² / 2^65 for n names. The writer then rejects the second asset with
  `DuplicateAssetId`, so a collision is detected but never resolved automatically. Names are not
  stored in the archive, so an archive can be listed by ID and type only.
- **CRC-32 detects accidental damage only**, not deliberate modification (see
  [Integrity](#integrity)). Version-1 archives cannot be checked for corruption at all.
- **`SaveToFile` is not atomic**: a crash while writing leaves a truncated file and the previous
  file is lost. The truncation is detected on the next open (`TruncatedArchive`). Writing to a
  temporary file and then renaming it would fix this.
- **The data is not aligned in the file** (assets are contiguous). Assets must be decoded with
  `ByteReader`, never cast to a struct; the library never does this.
- **`ArchiveReader` is not synchronised**: several threads may query an open archive at the same
  time, but `Open` and `Close` must not run concurrently with queries.
- At most 2^32 - 1 assets per archive (`assetCount` is a `uint32`).
- **Known defects**: none known at the time of writing. All 148 tests pass.

## Sources

- zlib Manual, *Checksum Functions* (`crc32`): https://www.zlib.net/manual.html. CRC-32 algorithm
  and usage (reference given in the brief).
- Ross N. Williams, *A Painless Guide to CRC Error Detection Algorithms* (1993):
  http://www.ross.net/crc/download/crc_v3.txt. The table-driven, reflected CRC algorithm.
- Greg Cook, *Catalogue of parametrised CRC algorithms* (CRC RevEng):
  https://reveng.sourceforge.io/crc-catalogue/all.htm. The CRC-32/ISO-HDLC parameters and the check
  value `0xCBF43926`.
- D. Eastlake et al., *The FNV Non-Cryptographic Hash Algorithm* (IETF draft):
  https://datatracker.ietf.org/doc/html/draft-eastlake-fnv. The FNV-1a 64-bit offset basis and
  prime used by `MakeAssetId`.
- FlatBuffers, *Internals*: https://flatbuffers.dev/internals/. Offsets and data organisation,
  used for comparison (reference given in the brief).
- Boost.Serialization, *Serialization and Versioning*:
  https://www.boost.org/doc/libs/release/libs/serialization/doc/tutorial.html. The principle of
  one version number per format, with the old versions still loadable (reference given in the
  brief).
- cppreference.com: `std::span`, `std::bit_cast`, and the replacement of the global
  `operator new` (https://en.cppreference.com/w/cpp/memory/new/operator_new), used by the memory
  measurements.

## Use of generative AI

Claude (Anthropic), used through Claude Code, generated most of the code, tests and documentation
of the Archive System part from the task list and this brief, one feature branch at a time. That
part covers Byte Access, Archive Format, Integrity, format versioning, the demo application and
the measurements. Every change was built with `/W4` in two configurations (x64 Debug, x86 Release)
and checked by the automated tests above. The measurements come from running the benchmark; they
were not estimated.

Inconsistencies found in the suggested solutions and corrected:
- A first test expected `InvalidMagic` for a 3-byte file. The reader correctly reports
  `TruncatedHeader`, because a magic number needs 4 bytes, so the test was wrong.
- A structured binding named `asset` hid the `asset` namespace inside a loop.
- The first version of format 2 rejected version-1 archives. This contradicted the brief, which
  requires the current reader to load the previous version; it was fixed by
  `feature/versioning-and-measurements`.
- The first benchmark counted its own timing vector as an allocation of the measured operation.

How the work was done: the student set the scope of each step from the task list and the brief.
Each feature was developed on its own branch, then built and tested in Visual Studio and committed
and pushed by the student. The code relies on no external library, so every algorithm (encoding,
CRC-32, FNV-1a, validation) is implemented in this repository and can be explained from it.

## Division of work

| Student | Part | Branches |
|---------|------|----------|
| Student 1: Archive System | Byte Access (`ByteWriter`, `ByteReader`, endianness, bounds checks), Archive Format (header, directory, IDs, types, offsets, multi-asset writing, loading by ID), Integrity (CRC-32, metadata validation, truncation and corruption detection), format versioning and migration, demo application, measurements | `feature/byte-access-layer`, `feature/archive-format`, `feature/integrity`, `feature/archive-demo`, `feature/versioning-and-measurements` |
| Student 2: Serialization | General-purpose serialization built on Byte Access: `SerializationTraits` (one specialisation per type, encoding and decoding), `Serializer` and `Deserializer` | `feature/Sérialisation` (in progress) |

**How the parts fit together**: Serialization turns a C++ object into bytes with `ByteWriter` and
back with `ByteReader`. The Archive layer stores those bytes as an asset, under a stable `AssetId`,
and gives them back by ID with integrity guaranteed. Neither layer depends on the other, which is
how the brief keeps the file format independent of the in-memory types.

**Integration status**: every Archive System branch builds on the previous one, so
`feature/versioning-and-measurements` contains all of them, and it is merged into `main`.
`feature/Sérialisation` is not merged yet. It starts from `feature/byte-access-layer`, and its headers are in
`AssetEngine/` rather than `AssetEngine/include/Serialization/`, so they will need to be moved
when it is merged.
