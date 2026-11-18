#include "pch.h"
#include "TestHelpers.h"

#include "ByteAccess/ByteReader.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace tests;

namespace ByteAccessTests
{
	// Compile-time API checks.
	static_assert(std::is_constructible_v<ByteReader, std::span<const std::byte>>);
	static_assert(std::is_constructible_v<ByteReader, const std::vector<std::byte>&>);
	static_assert(!std::is_constructible_v<ByteReader, std::vector<std::byte>&&>,
		"Constructing a reader from a temporary vector must not compile (dangling view)");
	static_assert(std::is_nothrow_default_constructible_v<ByteReader>);

	/*
	* Decoding tests: each test reads hand-written bytes, independently of the writer,
	* and checks values, position tracking and failure behaviour.
	*/
	TEST_CLASS(ByteReaderTests)
	{
	public:

		// --- Decoding --------------------------------------------------------------------

		TEST_METHOD(ReadUnsignedIntegersDecodeLittleEndian)
		{
			const std::vector<std::byte> buffer = Bytes({
				0xAB,
				0x22, 0x11,
				0x44, 0x33, 0x22, 0x11,
				0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 });
			ByteReader reader(buffer);

			std::uint8_t u8 = 0;
			std::uint16_t u16 = 0;
			std::uint32_t u32 = 0;
			std::uint64_t u64 = 0;
			AssertOk(reader.ReadUInt8(u8));
			AssertOk(reader.ReadUInt16(u16));
			AssertOk(reader.ReadUInt32(u32));
			AssertOk(reader.ReadUInt64(u64));

			Assert::AreEqual(std::uint8_t{ 0xAB }, u8);
			Assert::AreEqual(std::uint16_t{ 0x1122 }, u16);
			Assert::AreEqual(std::uint32_t{ 0x11223344u }, u32);
			Assert::AreEqual(std::uint64_t{ 0x1122334455667788ull }, u64);
			Assert::IsTrue(reader.IsAtEnd());
		}

		TEST_METHOD(ReadSignedIntegersDecodeTwosComplement)
		{
			const std::vector<std::byte> buffer = Bytes({
				0x80,
				0xFE, 0xFF,
				0x00, 0x00, 0x00, 0x80,
				0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0x7F });
			ByteReader reader(buffer);

			std::int8_t i8 = 0;
			std::int16_t i16 = 0;
			std::int32_t i32 = 0;
			std::int64_t i64 = 0;
			AssertOk(reader.ReadInt8(i8));
			AssertOk(reader.ReadInt16(i16));
			AssertOk(reader.ReadInt32(i32));
			AssertOk(reader.ReadInt64(i64));

			Assert::AreEqual(std::numeric_limits<std::int8_t>::min(), i8);
			Assert::AreEqual(std::int16_t{ -2 }, i16);
			Assert::AreEqual(std::numeric_limits<std::int32_t>::min(), i32);
			Assert::AreEqual(std::numeric_limits<std::int64_t>::max(), i64);
		}

		TEST_METHOD(ReadFloatingPointDecodesIeee754Bits)
		{
			const std::vector<std::byte> buffer = Bytes({
				0xC3, 0xF5, 0x48, 0x40,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F });
			ByteReader reader(buffer);

			float f = 0.0f;
			double d = 0.0;
			AssertOk(reader.ReadFloat(f));
			AssertOk(reader.ReadDouble(d));

			Assert::AreEqual(3.14f, f);
			Assert::AreEqual(1.0, d);
		}

		// --- Booleans --------------------------------------------------------------------

		TEST_METHOD(ReadBoolAcceptsZeroAndOne)
		{
			const std::vector<std::byte> buffer = Bytes({ 0x01, 0x00 });
			ByteReader reader(buffer);

			bool first = false;
			bool second = true;
			AssertOk(reader.ReadBool(first));
			AssertOk(reader.ReadBool(second));

			Assert::IsTrue(first);
			Assert::IsFalse(second);
		}

		TEST_METHOD(ReadBoolRejectsInvalidValuesWithoutSideEffects)
		{
			for (const unsigned invalid : { 0x02u, 0x7Fu, 0x80u, 0xFEu, 0xFFu })
			{
				const std::vector<std::byte> buffer = Bytes({ invalid });
				ByteReader reader(buffer);

				for (const bool initial : { false, true })
				{
					bool value = initial;
					AssertFails(ReadError::InvalidBool, reader.ReadBool(value));
					Assert::AreEqual(initial, value, L"A rejected bool must not modify the output");
					Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition(), L"A rejected bool must not advance the position");
				}

				// The reader stays usable: the same byte can still be read as an integer.
				std::uint8_t raw = 0;
				AssertOk(reader.ReadUInt8(raw));
				Assert::AreEqual(static_cast<std::uint8_t>(invalid), raw);
			}
		}

		// --- Empty buffers ---------------------------------------------------------------

		TEST_METHOD(DefaultReaderBehavesAsEmptyBuffer)
		{
			ByteReader reader;

			Assert::AreEqual(std::size_t{ 0 }, reader.GetSize());
			Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition());
			Assert::AreEqual(std::size_t{ 0 }, reader.GetRemaining());
			Assert::IsTrue(reader.IsAtEnd());

			std::uint8_t value = 0x5A;
			AssertFails(ReadError::EndOfBuffer, reader.ReadUInt8(value));
			Assert::AreEqual(std::uint8_t{ 0x5A }, value);
		}

		TEST_METHOD(EmptyBufferRejectsEveryRead)
		{
			const std::vector<std::byte> empty;
			ByteReader reader(empty);

			std::uint8_t u8 = 1;     std::uint16_t u16 = 2;   std::uint32_t u32 = 3;   std::uint64_t u64 = 4;
			std::int8_t i8 = 5;      std::int16_t i16 = 6;    std::int32_t i32 = 7;    std::int64_t i64 = 8;
			float f = 9.0f;          double d = 10.0;         bool b = true;
			std::array<std::byte, 1> bytes{ std::byte{ 0x33 } };
			std::span<const std::byte> view = bytes;

			AssertFails(ReadError::EndOfBuffer, reader.ReadUInt8(u8));
			AssertFails(ReadError::EndOfBuffer, reader.ReadUInt16(u16));
			AssertFails(ReadError::EndOfBuffer, reader.ReadUInt32(u32));
			AssertFails(ReadError::EndOfBuffer, reader.ReadUInt64(u64));
			AssertFails(ReadError::EndOfBuffer, reader.ReadInt8(i8));
			AssertFails(ReadError::EndOfBuffer, reader.ReadInt16(i16));
			AssertFails(ReadError::EndOfBuffer, reader.ReadInt32(i32));
			AssertFails(ReadError::EndOfBuffer, reader.ReadInt64(i64));
			AssertFails(ReadError::EndOfBuffer, reader.ReadFloat(f));
			AssertFails(ReadError::EndOfBuffer, reader.ReadDouble(d));
			AssertFails(ReadError::EndOfBuffer, reader.ReadBool(b));
			AssertFails(ReadError::EndOfBuffer, reader.ReadBytes(bytes));
			AssertFails(ReadError::EndOfBuffer, reader.ReadBytesView(1, view));
			AssertFails(ReadError::EndOfBuffer, reader.Skip(1));
			AssertFails(ReadError::EndOfBuffer, reader.Seek(1));

			// Outputs untouched.
			Assert::AreEqual(std::uint8_t{ 1 }, u8);    Assert::AreEqual(std::uint16_t{ 2 }, u16);
			Assert::AreEqual(std::uint32_t{ 3 }, u32);  Assert::AreEqual(std::uint64_t{ 4 }, u64);
			Assert::AreEqual(std::int8_t{ 5 }, i8);     Assert::AreEqual(std::int16_t{ 6 }, i16);
			Assert::AreEqual(std::int32_t{ 7 }, i32);   Assert::AreEqual(std::int64_t{ 8 }, i64);
			Assert::AreEqual(9.0f, f);                  Assert::AreEqual(10.0, d);
			Assert::IsTrue(b);
			Assert::AreEqual(std::byte{ 0x33 }, bytes[0]);
			Assert::IsTrue(view.data() == bytes.data() && view.size() == 1);

			// Zero-sized operations are valid on an empty buffer.
			AssertOk(reader.ReadBytes({}));
			AssertOk(reader.ReadBytesView(0, view));
			Assert::IsTrue(view.empty());
			AssertOk(reader.Skip(0));
			AssertOk(reader.Seek(0));
			Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition());
		}

		// --- Reading past the end --------------------------------------------------------

		TEST_METHOD(ReadPastEndFailsWithoutSideEffects)
		{
			const std::vector<std::byte> buffer = Bytes({ 0x01, 0x02, 0x03 });
			ByteReader reader(buffer);

			std::uint32_t big = 0xCAFEBABEu;
			AssertFails(ReadError::EndOfBuffer, reader.ReadUInt32(big));
			Assert::AreEqual(std::uint32_t{ 0xCAFEBABEu }, big, L"Failed read must not modify the output");
			Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition(), L"Failed read must not advance the position");

			std::uint16_t half = 0;
			AssertOk(reader.ReadUInt16(half));
			Assert::AreEqual(std::uint16_t{ 0x0201 }, half);

			// One byte left: a 2-byte read fails, a 1-byte read succeeds.
			half = 0x7777;
			AssertFails(ReadError::EndOfBuffer, reader.ReadUInt16(half));
			Assert::AreEqual(std::uint16_t{ 0x7777 }, half);
			Assert::AreEqual(std::size_t{ 2 }, reader.GetPosition());

			std::uint8_t last = 0;
			AssertOk(reader.ReadUInt8(last));
			Assert::AreEqual(std::uint8_t{ 0x03 }, last);
			Assert::IsTrue(reader.IsAtEnd());

			AssertFails(ReadError::EndOfBuffer, reader.ReadUInt8(last));
			Assert::AreEqual(std::uint8_t{ 0x03 }, last);
			Assert::AreEqual(std::size_t{ 3 }, reader.GetPosition());
		}

		TEST_METHOD(EveryTypeFailsWhenOneByteIsMissing)
		{
			// For each type of size N, a buffer of N - 1 bytes must be rejected.
			const std::vector<std::byte> buffer = Bytes({ 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF });
			const std::span<const std::byte> all = buffer;

			std::uint16_t u16 = 0; std::int16_t i16 = 0;
			std::uint32_t u32 = 0; std::int32_t i32 = 0; float f = 0.0f;
			std::uint64_t u64 = 0; std::int64_t i64 = 0; double d = 0.0;

			{ ByteReader r(all.first(1)); AssertFails(ReadError::EndOfBuffer, r.ReadUInt16(u16)); AssertFails(ReadError::EndOfBuffer, r.ReadInt16(i16)); Assert::AreEqual(std::size_t{ 0 }, r.GetPosition()); }
			{ ByteReader r(all.first(3)); AssertFails(ReadError::EndOfBuffer, r.ReadUInt32(u32)); AssertFails(ReadError::EndOfBuffer, r.ReadInt32(i32)); AssertFails(ReadError::EndOfBuffer, r.ReadFloat(f)); Assert::AreEqual(std::size_t{ 0 }, r.GetPosition()); }
			{ ByteReader r(all.first(7)); AssertFails(ReadError::EndOfBuffer, r.ReadUInt64(u64)); AssertFails(ReadError::EndOfBuffer, r.ReadInt64(i64)); AssertFails(ReadError::EndOfBuffer, r.ReadDouble(d)); Assert::AreEqual(std::size_t{ 0 }, r.GetPosition()); }

			Assert::IsTrue(u16 == 0 && i16 == 0 && u32 == 0 && i32 == 0 && u64 == 0 && i64 == 0);
			Assert::IsTrue(f == 0.0f && d == 0.0);
		}

		TEST_METHOD(HugeCountsDoNotOverflowTheBoundsCheck)
		{
			// A corrupted length field may contain any value: position + count must not wrap around.
			const std::vector<std::byte> buffer = Bytes({ 0x01, 0x02, 0x03, 0x04 });
			ByteReader reader(buffer);
			AssertOk(reader.Skip(1));

			constexpr std::size_t huge = std::numeric_limits<std::size_t>::max();
			std::span<const std::byte> view;

			Assert::IsFalse(reader.CanRead(huge));
			Assert::IsFalse(reader.CanRead(huge - 2)); // 1 + (SIZE_MAX - 2) would not wrap, but still exceeds the 3 bytes left
			AssertFails(ReadError::EndOfBuffer, reader.Skip(huge));
			AssertFails(ReadError::EndOfBuffer, reader.ReadBytesView(huge, view));
			AssertFails(ReadError::EndOfBuffer, reader.Seek(huge));
			Assert::AreEqual(std::size_t{ 1 }, reader.GetPosition());
			Assert::IsTrue(view.empty());
		}

		// --- Raw bytes -------------------------------------------------------------------

		TEST_METHOD(ReadBytesCopiesAndAdvances)
		{
			const std::vector<std::byte> buffer = Bytes({ 0x10, 0x20, 0x30, 0x40, 0x50 });
			ByteReader reader(buffer);
			AssertOk(reader.Skip(1));

			std::array<std::byte, 3> destination{};
			AssertOk(reader.ReadBytes(destination));

			AssertBytesEqual(Bytes({ 0x20, 0x30, 0x40 }), destination);
			Assert::AreEqual(std::size_t{ 4 }, reader.GetPosition());
		}

		TEST_METHOD(ReadBytesPastEndLeavesDestinationUntouched)
		{
			const std::vector<std::byte> buffer = Bytes({ 0x10, 0x20 });
			ByteReader reader(buffer);

			std::array<std::byte, 3> destination{ std::byte{ 0xAA }, std::byte{ 0xBB }, std::byte{ 0xCC } };
			AssertFails(ReadError::EndOfBuffer, reader.ReadBytes(destination));

			AssertBytesEqual(Bytes({ 0xAA, 0xBB, 0xCC }), destination);
			Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition());
		}

		TEST_METHOD(ReadBytesViewIsZeroCopy)
		{
			const std::vector<std::byte> buffer = Bytes({ 0x01, 0x02, 0x03, 0x04 });
			ByteReader reader(buffer);
			AssertOk(reader.Skip(1));

			std::span<const std::byte> view;
			AssertOk(reader.ReadBytesView(2, view));

			Assert::IsTrue(view.data() == buffer.data() + 1, L"The view must point into the original buffer");
			Assert::AreEqual(std::size_t{ 2 }, view.size());
			Assert::AreEqual(std::size_t{ 3 }, reader.GetPosition());

			// A view can be used to build a bounded sub-reader.
			ByteReader sub(view);
			std::uint16_t value = 0;
			AssertOk(sub.ReadUInt16(value));
			Assert::AreEqual(std::uint16_t{ 0x0302 }, value);
			Assert::IsTrue(sub.IsAtEnd());
		}

		TEST_METHOD(ReaderDoesNotCopyTheBuffer)
		{
			const std::vector<std::byte> buffer = Bytes({ 0x01, 0x02 });
			const ByteReader reader(buffer);

			Assert::IsTrue(reader.GetBuffer().data() == buffer.data());
			Assert::AreEqual(buffer.size(), reader.GetSize());
		}

		// --- Position tracking -----------------------------------------------------------

		TEST_METHOD(PositionAndRemainingTrackEveryRead)
		{
			const std::vector<std::byte> buffer(1 + 2 + 4 + 8 + 4 + 8 + 1 + 3, std::byte{ 0 });
			ByteReader reader(buffer);
			const std::size_t size = buffer.size();

			auto expectPosition = [&](std::size_t expected)
			{
				Assert::AreEqual(expected, reader.GetPosition());
				Assert::AreEqual(size - expected, reader.GetRemaining());
				Assert::AreEqual(expected == size, reader.IsAtEnd());
			};

			std::uint8_t u8 = 0; std::int16_t i16 = 0; std::uint32_t u32 = 0; std::int64_t i64 = 0;
			float f = 0.0f; double d = 0.0; bool b = false; std::array<std::byte, 3> raw{};

			expectPosition(0);
			AssertOk(reader.ReadUInt8(u8));   expectPosition(1);
			AssertOk(reader.ReadInt16(i16));  expectPosition(3);
			AssertOk(reader.ReadUInt32(u32)); expectPosition(7);
			AssertOk(reader.ReadInt64(i64));  expectPosition(15);
			AssertOk(reader.ReadFloat(f));    expectPosition(19);
			AssertOk(reader.ReadDouble(d));   expectPosition(27);
			AssertOk(reader.ReadBool(b));     expectPosition(28);
			AssertOk(reader.ReadBytes(raw));  expectPosition(31);
		}

		TEST_METHOD(SkipAndSeekMoveThePosition)
		{
			const std::vector<std::byte> buffer = Bytes({ 0x00, 0x11, 0x22, 0x33 });
			ByteReader reader(buffer);

			AssertOk(reader.Skip(2));
			Assert::AreEqual(std::size_t{ 2 }, reader.GetPosition());

			std::uint8_t value = 0;
			AssertOk(reader.ReadUInt8(value));
			Assert::AreEqual(std::uint8_t{ 0x22 }, value);

			AssertOk(reader.Seek(1));
			AssertOk(reader.ReadUInt8(value));
			Assert::AreEqual(std::uint8_t{ 0x11 }, value);

			AssertOk(reader.Seek(4)); // "at end" is a valid position
			Assert::IsTrue(reader.IsAtEnd());

			AssertFails(ReadError::EndOfBuffer, reader.Seek(5));
			AssertFails(ReadError::EndOfBuffer, reader.Skip(1));
			Assert::AreEqual(std::size_t{ 4 }, reader.GetPosition());
		}

		TEST_METHOD(CanReadReportsAvailableBytes)
		{
			const std::vector<std::byte> buffer = Bytes({ 0x00, 0x00, 0x00 });
			ByteReader reader(buffer);

			Assert::IsTrue(reader.CanRead(0));
			Assert::IsTrue(reader.CanRead(3));
			Assert::IsFalse(reader.CanRead(4));

			AssertOk(reader.Skip(3));
			Assert::IsTrue(reader.CanRead(0));
			Assert::IsFalse(reader.CanRead(1));
		}

		// --- ReadResult ------------------------------------------------------------------

		TEST_METHOD(ReadResultConversionsAndComparisons)
		{
			constexpr ReadResult ok;
			constexpr ReadResult endOfBuffer = ReadError::EndOfBuffer;

			static_assert(ok.IsOk() && static_cast<bool>(ok));
			static_assert(!endOfBuffer.IsOk() && !static_cast<bool>(endOfBuffer));
			static_assert(ok == ReadError::None);
			static_assert(endOfBuffer == ReadError::EndOfBuffer);
			static_assert(endOfBuffer != ReadError::InvalidBool);
			static_assert(endOfBuffer.GetError() == ReadError::EndOfBuffer);

			Assert::IsFalse(asset::ToString(ReadError::None).empty());
			Assert::IsFalse(asset::ToString(ReadError::EndOfBuffer).empty());
			Assert::IsFalse(asset::ToString(ReadError::InvalidBool).empty());
		}
	};
}
