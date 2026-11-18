#include "pch.h"
#include "TestHelpers.h"

#include "ByteAccess/ByteReader.h"
#include "ByteAccess/ByteWriter.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace tests;

namespace ByteAccessTests
{
	namespace
	{
		/**
		 * Writes every value with `write`, checks the encoded size, then reads them back with `read`
		 * and checks each value and that the whole buffer was consumed.
		 */
		template <typename T>
		void RoundTrip(void (ByteWriter::* write)(T), ReadResult(ByteReader::* read)(T&) noexcept,
			std::initializer_list<T> values, std::size_t encodedSize)
		{
			ByteWriter writer;
			for (const T value : values)
			{
				(writer.*write)(value);
			}
			Assert::AreEqual(values.size() * encodedSize, writer.GetSize(), L"Unexpected encoded size");

			ByteReader reader(writer.GetBuffer());
			for (const T expected : values)
			{
				T actual{};
				AssertOk((reader.*read)(actual));
				Assert::AreEqual(expected, actual);
			}
			Assert::IsTrue(reader.IsAtEnd());
		}

		/** Same as RoundTrip for floating-point values, but compares bit patterns (so NaN and -0.0 are checked exactly). */
		template <typename T, typename Bits>
		void RoundTripBits(void (ByteWriter::* write)(T), ReadResult(ByteReader::* read)(T&) noexcept,
			std::initializer_list<T> values)
		{
			ByteWriter writer;
			for (const T value : values)
			{
				(writer.*write)(value);
			}
			Assert::AreEqual(values.size() * sizeof(T), writer.GetSize(), L"Unexpected encoded size");

			ByteReader reader(writer.GetBuffer());
			for (const T expected : values)
			{
				T actual{};
				AssertOk((reader.*read)(actual));
				Assert::AreEqual(std::bit_cast<Bits>(expected), std::bit_cast<Bits>(actual), L"Bit pattern changed");
			}
			Assert::IsTrue(reader.IsAtEnd());
		}
	}

	/*
	* Round-trip tests: whatever the writer produces, the reader must give back exactly.
	*/
	TEST_CLASS(RoundTripTests)
	{
	public:

		TEST_METHOD(UnsignedIntegers)
		{
			using L8 = std::numeric_limits<std::uint8_t>;
			using L16 = std::numeric_limits<std::uint16_t>;
			using L32 = std::numeric_limits<std::uint32_t>;
			using L64 = std::numeric_limits<std::uint64_t>;

			RoundTrip<std::uint8_t>(&ByteWriter::WriteUInt8, &ByteReader::ReadUInt8, { 0, 1, 0x7F, 0x80, L8::max() }, 1);
			RoundTrip<std::uint16_t>(&ByteWriter::WriteUInt16, &ByteReader::ReadUInt16, { 0, 1, 0x00FF, 0x0100, 0x8000, L16::max() }, 2);
			RoundTrip<std::uint32_t>(&ByteWriter::WriteUInt32, &ByteReader::ReadUInt32, { 0, 1, 0x000000FF, 0x80000000, 0x12345678, L32::max() }, 4);
			RoundTrip<std::uint64_t>(&ByteWriter::WriteUInt64, &ByteReader::ReadUInt64, { 0, 1, 0xFFFFFFFFull, 0x100000000ull, 0x8000000000000000ull, L64::max() }, 8);
		}

		TEST_METHOD(SignedIntegers)
		{
			using L8 = std::numeric_limits<std::int8_t>;
			using L16 = std::numeric_limits<std::int16_t>;
			using L32 = std::numeric_limits<std::int32_t>;
			using L64 = std::numeric_limits<std::int64_t>;

			RoundTrip<std::int8_t>(&ByteWriter::WriteInt8, &ByteReader::ReadInt8, { 0, 1, -1, L8::min(), L8::max() }, 1);
			RoundTrip<std::int16_t>(&ByteWriter::WriteInt16, &ByteReader::ReadInt16, { 0, 1, -1, -256, L16::min(), L16::max() }, 2);
			RoundTrip<std::int32_t>(&ByteWriter::WriteInt32, &ByteReader::ReadInt32, { 0, 1, -1, -123456789, L32::min(), L32::max() }, 4);
			RoundTrip<std::int64_t>(&ByteWriter::WriteInt64, &ByteReader::ReadInt64, { 0, 1, -1, -1234567890123ll, L64::min(), L64::max() }, 8);
		}

		TEST_METHOD(FloatPreservesEveryBitPattern)
		{
			using L = std::numeric_limits<float>;
			RoundTripBits<float, std::uint32_t>(&ByteWriter::WriteFloat, &ByteReader::ReadFloat, {
				0.0f, -0.0f, 1.0f, -1.0f, 3.14159265f, 1.0e-30f,
				L::min(), L::denorm_min(), L::max(), L::lowest(), L::epsilon(),
				L::infinity(), -L::infinity(), L::quiet_NaN(),
				std::bit_cast<float>(0x7FC12345u), // NaN with a custom payload
			});
		}

		TEST_METHOD(DoublePreservesEveryBitPattern)
		{
			using L = std::numeric_limits<double>;
			RoundTripBits<double, std::uint64_t>(&ByteWriter::WriteDouble, &ByteReader::ReadDouble, {
				0.0, -0.0, 1.0, -1.0, 3.141592653589793, 1.0e-300,
				L::min(), L::denorm_min(), L::max(), L::lowest(), L::epsilon(),
				L::infinity(), -L::infinity(), L::quiet_NaN(),
				std::bit_cast<double>(0x7FF8000012345678ull), // NaN with a custom payload
			});
		}

		TEST_METHOD(Booleans)
		{
			RoundTrip<bool>(&ByteWriter::WriteBool, &ByteReader::ReadBool, { true, false, false, true }, 1);
		}

		TEST_METHOD(RawBytesAllValues)
		{
			std::vector<std::byte> payload(256);
			for (std::size_t i = 0; i < payload.size(); ++i)
			{
				payload[i] = static_cast<std::byte>(i);
			}

			ByteWriter writer;
			writer.WriteBytes(payload);

			ByteReader reader(writer.GetBuffer());
			std::vector<std::byte> copy(payload.size());
			AssertOk(reader.ReadBytes(copy));

			AssertBytesEqual(payload, copy);
			Assert::IsTrue(reader.IsAtEnd());
		}

		TEST_METHOD(MixedSequenceOfConsecutiveWritesAndReads)
		{
			const std::vector<std::byte> blob = Bytes({ 'a', 's', 's', 'e', 't' });

			ByteWriter writer;
			writer.WriteUInt8(7);
			writer.WriteInt32(-42);
			writer.WriteDouble(-9.81);
			writer.WriteBool(false);
			writer.WriteUInt32(static_cast<std::uint32_t>(blob.size())); // length prefix, as a serializer would do
			writer.WriteBytes(blob);
			writer.WriteInt64(std::numeric_limits<std::int64_t>::min());
			writer.WriteFloat(0.5f);
			writer.WriteUInt16(65535);
			writer.WriteBool(true);

			ByteReader reader(writer.GetBuffer());

			std::uint8_t u8 = 0;
			std::int32_t i32 = 0;
			double d = 0.0;
			bool b1 = true;
			std::uint32_t length = 0;
			std::span<const std::byte> blobView;
			std::int64_t i64 = 0;
			float f = 0.0f;
			std::uint16_t u16 = 0;
			bool b2 = false;

			AssertOk(reader.ReadUInt8(u8));
			AssertOk(reader.ReadInt32(i32));
			AssertOk(reader.ReadDouble(d));
			AssertOk(reader.ReadBool(b1));
			AssertOk(reader.ReadUInt32(length));
			AssertOk(reader.ReadBytesView(length, blobView));
			AssertOk(reader.ReadInt64(i64));
			AssertOk(reader.ReadFloat(f));
			AssertOk(reader.ReadUInt16(u16));
			AssertOk(reader.ReadBool(b2));

			Assert::AreEqual(std::uint8_t{ 7 }, u8);
			Assert::AreEqual(-42, i32);
			Assert::AreEqual(-9.81, d);
			Assert::IsFalse(b1);
			Assert::AreEqual(std::uint32_t{ 5 }, length);
			AssertBytesEqual(blob, blobView);
			Assert::AreEqual(std::numeric_limits<std::int64_t>::min(), i64);
			Assert::AreEqual(0.5f, f);
			Assert::AreEqual(std::uint16_t{ 65535 }, u16);
			Assert::IsTrue(b2);
			Assert::IsTrue(reader.IsAtEnd());

			// Nothing left: any further read fails.
			std::uint8_t extra = 0;
			AssertFails(ReadError::EndOfBuffer, reader.ReadUInt8(extra));
		}

		TEST_METHOD(IntegrationExampleFromSpecification)
		{
			ByteWriter writer;

			writer.WriteUInt32(42);
			writer.WriteFloat(3.14f);
			writer.WriteBool(true);

			ByteReader reader(writer.GetBuffer());

			std::uint32_t value = 0;
			float floatingValue = 0.0f;
			bool booleanValue = false;

			AssertOk(reader.ReadUInt32(value));
			AssertOk(reader.ReadFloat(floatingValue));
			AssertOk(reader.ReadBool(booleanValue));

			Assert::AreEqual(std::uint32_t{ 42 }, value);
			Assert::AreEqual(3.14f, floatingValue);
			Assert::IsTrue(booleanValue);
			Assert::AreEqual(std::size_t{ 9 }, writer.GetSize());
		}

		TEST_METHOD(ReleasedBufferCanBeRead)
		{
			ByteWriter writer;
			writer.WriteInt16(-300);
			const std::vector<std::byte> owned = writer.ReleaseBuffer();

			ByteReader reader(owned);
			std::int16_t value = 0;
			AssertOk(reader.ReadInt16(value));
			Assert::AreEqual(std::int16_t{ -300 }, value);
		}

		TEST_METHOD(LargeSequence)
		{
			constexpr std::uint32_t count = 10'000;

			ByteWriter writer(count * sizeof(std::uint32_t));
			for (std::uint32_t i = 0; i < count; ++i)
			{
				writer.WriteUInt32(i * 2654435761u); // spread the bits over the whole range
			}
			Assert::AreEqual(std::size_t{ count * 4 }, writer.GetSize());

			ByteReader reader(writer.GetBuffer());
			for (std::uint32_t i = 0; i < count; ++i)
			{
				std::uint32_t value = 0;
				AssertOk(reader.ReadUInt32(value));
				Assert::AreEqual(i * 2654435761u, value);
			}
			Assert::IsTrue(reader.IsAtEnd());
		}
	};
}
