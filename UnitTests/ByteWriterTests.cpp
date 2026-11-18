#include "pch.h"
#include "TestHelpers.h"

#include "ByteAccess/ByteWriter.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace tests;

namespace ByteAccessTests
{
	/*
	* Encoding tests: each test checks the exact bytes produced by the writer,
	* independently of the reader, to pin down the binary format.
	*/
	TEST_CLASS(ByteWriterTests)
	{
	public:

		TEST_METHOD(NewWriterIsEmpty)
		{
			const ByteWriter writer;

			Assert::IsTrue(writer.IsEmpty());
			Assert::AreEqual(std::size_t{ 0 }, writer.GetSize());
			Assert::IsTrue(writer.GetBuffer().empty());
		}

		TEST_METHOD(ReserveDoesNotWriteAnything)
		{
			ByteWriter writer(64);
			writer.Reserve(128);

			Assert::IsTrue(writer.IsEmpty());
			Assert::AreEqual(std::size_t{ 0 }, writer.GetSize());
		}

		TEST_METHOD(WriteUInt8)
		{
			ByteWriter writer;
			writer.WriteUInt8(0x00);
			writer.WriteUInt8(0xAB);
			writer.WriteUInt8(0xFF);

			AssertBytesEqual(Bytes({ 0x00, 0xAB, 0xFF }), writer.GetBuffer());
		}

		TEST_METHOD(WriteUInt16IsLittleEndian)
		{
			ByteWriter writer;
			writer.WriteUInt16(0x1122);

			AssertBytesEqual(Bytes({ 0x22, 0x11 }), writer.GetBuffer());
		}

		TEST_METHOD(WriteUInt32IsLittleEndian)
		{
			ByteWriter writer;
			writer.WriteUInt32(0x11223344u);

			AssertBytesEqual(Bytes({ 0x44, 0x33, 0x22, 0x11 }), writer.GetBuffer());
		}

		TEST_METHOD(WriteUInt64IsLittleEndian)
		{
			ByteWriter writer;
			writer.WriteUInt64(0x1122334455667788ull);

			AssertBytesEqual(Bytes({ 0x88, 0x77, 0x66, 0x55, 0x44, 0x33, 0x22, 0x11 }), writer.GetBuffer());
		}

		TEST_METHOD(WriteInt8UsesTwosComplement)
		{
			ByteWriter writer;
			writer.WriteInt8(-1);
			writer.WriteInt8(std::numeric_limits<std::int8_t>::min());
			writer.WriteInt8(std::numeric_limits<std::int8_t>::max());

			AssertBytesEqual(Bytes({ 0xFF, 0x80, 0x7F }), writer.GetBuffer());
		}

		TEST_METHOD(WriteInt16UsesTwosComplementLittleEndian)
		{
			ByteWriter writer;
			writer.WriteInt16(-2);
			writer.WriteInt16(std::numeric_limits<std::int16_t>::min());

			AssertBytesEqual(Bytes({ 0xFE, 0xFF, 0x00, 0x80 }), writer.GetBuffer());
		}

		TEST_METHOD(WriteInt32UsesTwosComplementLittleEndian)
		{
			ByteWriter writer;
			writer.WriteInt32(-2);
			writer.WriteInt32(std::numeric_limits<std::int32_t>::min());

			AssertBytesEqual(Bytes({ 0xFE, 0xFF, 0xFF, 0xFF, 0x00, 0x00, 0x00, 0x80 }), writer.GetBuffer());
		}

		TEST_METHOD(WriteInt64UsesTwosComplementLittleEndian)
		{
			ByteWriter writer;
			writer.WriteInt64(-2);
			writer.WriteInt64(std::numeric_limits<std::int64_t>::min());

			AssertBytesEqual(Bytes({
				0xFE, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF, 0xFF,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80 }), writer.GetBuffer());
		}

		TEST_METHOD(WriteFloatUsesIeee754BitsLittleEndian)
		{
			ByteWriter writer;
			writer.WriteFloat(1.0f);   // 0x3F800000
			writer.WriteFloat(-2.0f);  // 0xC0000000
			writer.WriteFloat(3.14f);  // 0x4048F5C3

			AssertBytesEqual(Bytes({
				0x00, 0x00, 0x80, 0x3F,
				0x00, 0x00, 0x00, 0xC0,
				0xC3, 0xF5, 0x48, 0x40 }), writer.GetBuffer());
		}

		TEST_METHOD(WriteDoubleUsesIeee754BitsLittleEndian)
		{
			ByteWriter writer;
			writer.WriteDouble(1.0);   // 0x3FF0000000000000
			writer.WriteDouble(-0.0);  // 0x8000000000000000

			AssertBytesEqual(Bytes({
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0xF0, 0x3F,
				0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x80 }), writer.GetBuffer());
		}

		TEST_METHOD(WriteBoolIsExactlyOneByteZeroOrOne)
		{
			ByteWriter writer;
			writer.WriteBool(true);
			writer.WriteBool(false);

			AssertBytesEqual(Bytes({ 0x01, 0x00 }), writer.GetBuffer());
		}

		TEST_METHOD(WriteBytesAppendsAsIs)
		{
			const std::vector<std::byte> payload = Bytes({ 0xDE, 0xAD, 0xBE, 0xEF });

			ByteWriter writer;
			writer.WriteUInt8(0x01);
			writer.WriteBytes(payload);

			AssertBytesEqual(Bytes({ 0x01, 0xDE, 0xAD, 0xBE, 0xEF }), writer.GetBuffer());
		}

		TEST_METHOD(WriteEmptyBytesDoesNothing)
		{
			ByteWriter writer;
			writer.WriteBytes({});
			Assert::IsTrue(writer.IsEmpty());

			writer.WriteUInt8(0x42);
			writer.WriteBytes(std::span<const std::byte>{});
			AssertBytesEqual(Bytes({ 0x42 }), writer.GetBuffer());
		}

		TEST_METHOD(WriteBytesFromOwnBufferIsSafe)
		{
			// Appending a view of the writer's own storage must work even when the vector reallocates.
			ByteWriter writer;
			writer.WriteBytes(Bytes({ 0x01, 0x02, 0x03 }));

			writer.WriteBytes(writer.GetBuffer());
			AssertBytesEqual(Bytes({ 0x01, 0x02, 0x03, 0x01, 0x02, 0x03 }), writer.GetBuffer());

			writer.WriteBytes(writer.GetBuffer().subspan(1, 2));
			AssertBytesEqual(Bytes({ 0x01, 0x02, 0x03, 0x01, 0x02, 0x03, 0x02, 0x03 }), writer.GetBuffer());
		}

		TEST_METHOD(ConsecutiveWritesAreContiguousWithoutPadding)
		{
			ByteWriter writer;
			writer.WriteUInt8(0xAA);
			writer.WriteUInt32(0x01020304u);
			writer.WriteBool(true);
			writer.WriteInt16(-1);
			writer.WriteUInt64(1);

			Assert::AreEqual(std::size_t{ 1 + 4 + 1 + 2 + 8 }, writer.GetSize());
			AssertBytesEqual(Bytes({
				0xAA,
				0x04, 0x03, 0x02, 0x01,
				0x01,
				0xFF, 0xFF,
				0x01, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00, 0x00 }), writer.GetBuffer());
		}

		TEST_METHOD(ClearEmptiesTheWriterAndKeepsItUsable)
		{
			ByteWriter writer;
			writer.WriteUInt32(0xFFFFFFFFu);
			writer.Clear();

			Assert::IsTrue(writer.IsEmpty());

			writer.WriteUInt8(0x07);
			AssertBytesEqual(Bytes({ 0x07 }), writer.GetBuffer());
		}

		TEST_METHOD(ReleaseBufferMovesBytesWithoutCopyAndEmptiesWriter)
		{
			ByteWriter writer;
			writer.WriteUInt16(0xBEEF);
			const std::byte* const storage = writer.GetBuffer().data();

			const std::vector<std::byte> released = writer.ReleaseBuffer();

			AssertBytesEqual(Bytes({ 0xEF, 0xBE }), released);
			Assert::IsTrue(released.data() == storage, L"The buffer must be moved, not copied");
			Assert::IsTrue(writer.IsEmpty());

			writer.WriteUInt8(0x01);
			AssertBytesEqual(Bytes({ 0x01 }), writer.GetBuffer());
		}
	};
}
