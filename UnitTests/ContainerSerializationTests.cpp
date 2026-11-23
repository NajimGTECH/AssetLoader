#include "pch.h"
#include "TestHelpers.h"

#include <stdexcept>

#include "ByteAccess/ByteReader.h"
#include "ByteAccess/ByteWriter.h"
#include "Serialization/Deserializer.h"
#include "Serialization/Serializer.h"
#include "Serialization/Utf8.h"
#include "Types/Vector3.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace tests;

/*
* Core encodings of strings (UTF-8), variable-length sequences and fixed-size arrays:
* exact bytes, round-trips, empty values, and rejection of invalid or excessive data
* before any allocation.
*/
namespace ContainerSerializationTests
{
	namespace
	{
		template<typename T>
		std::vector<std::byte> Encode(const T& value)
		{
			Serializer serializer;
			serializer.Write(value);
			return serializer.ReleaseBuffer();
		}

		template<typename T>
		T RoundTrip(const T& value)
		{
			const std::vector<std::byte> bytes = Encode(value);
			Deserializer deserializer(bytes);
			T decoded{};
			AssertOk(deserializer.Read(decoded));
			Assert::IsTrue(deserializer.IsAtEnd(), L"Every byte must be consumed");
			return decoded;
		}

		/** Checks that reading fails with `expected`, leaves the value untouched and does not move the position. */
		template<typename T>
		void AssertRejected(ReadError expected, std::span<const std::byte> bytes, const T& sentinel)
		{
			ByteReader reader(bytes);
			T value = sentinel;
			AssertFails(expected, SerializationTraits<T>::Deserialize(reader, value));
			Assert::IsTrue(value == sentinel, L"Value must be untouched on failure");
			Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition(), L"Position must be restored on failure");
		}
	}

	TEST_CLASS(StringTests)
	{
	public:
		TEST_METHOD(EncodingIsLengthThenUtf8Bytes)
		{
			// "hé" = 68 C3 A9 in UTF-8: length 3 (little-endian uint32), then the bytes, no terminator.
			AssertBytesEqual(Bytes({ 0x03, 0x00, 0x00, 0x00, 0x68, 0xC3, 0xA9 }), Encode(std::string("h\xC3\xA9")));
		}

		TEST_METHOD(EmptyStringIsFourZeroBytes)
		{
			AssertBytesEqual(Bytes({ 0x00, 0x00, 0x00, 0x00 }), Encode(std::string()));
			Assert::IsTrue(RoundTrip(std::string()).empty());
		}

		TEST_METHOD(RoundTripsAsciiAndMultibyteText)
		{
			const std::string samples[] = {
				"Hello, world",
				"Caf\xC3\xA9 cr\xC3\xA8me br\xC3\xBBl\xC3\xA9" /* Café crème brûlé */,
				"\xE6\x97\xA5\xE6\x9C\xAC\xE8\xAA\x9E" /* 日本語 */,
				"\xF0\x9F\x8E\xAE" /* U+1F3AE, 4 bytes */,
				std::string("zero\0inside", 11),
			};
			for (const std::string& sample : samples)
			{
				Assert::IsTrue(sample == RoundTrip(sample));
			}
		}

		TEST_METHOD(InvalidUtf8IsRejectedOnRead)
		{
			const std::vector<std::vector<std::byte>> invalid = {
				Bytes({ 0x01, 0, 0, 0, 0x80 }),                   // lone continuation byte
				Bytes({ 0x02, 0, 0, 0, 0xC0, 0xAF }),             // overlong '/'
				Bytes({ 0x03, 0, 0, 0, 0xE0, 0x80, 0xAF }),       // overlong 3-byte form
				Bytes({ 0x03, 0, 0, 0, 0xED, 0xA0, 0x80 }),       // UTF-16 surrogate U+D800
				Bytes({ 0x04, 0, 0, 0, 0xF4, 0x90, 0x80, 0x80 }), // above U+10FFFF
				Bytes({ 0x02, 0, 0, 0, 0xE2, 0x82 }),             // truncated sequence
				Bytes({ 0x01, 0, 0, 0, 0xFF }),                   // never valid
			};
			for (const auto& bytes : invalid)
			{
				AssertRejected<std::string>(ReadError::InvalidUtf8, bytes, "untouched");
			}
		}

		TEST_METHOD(InvalidUtf8IsRefusedOnWrite)
		{
			Assert::ExpectException<std::invalid_argument>([] { (void)Encode(std::string("\xC0\xAF")); });
		}

		TEST_METHOD(LengthLongerThanTheDataIsRejected)
		{
			AssertRejected<std::string>(ReadError::LengthTooLarge, Bytes({ 0x05, 0, 0, 0, 'a', 'b' }), "untouched");
		}

		TEST_METHOD(HugeLengthIsRejectedWithoutAllocating)
		{
			// 0xFFFFFFFF bytes announced by 4 bytes of data: refused before anything is reserved.
			AssertRejected<std::string>(ReadError::LengthTooLarge, Bytes({ 0xFF, 0xFF, 0xFF, 0xFF }), "untouched");
		}

		TEST_METHOD(TruncatedLengthIsEndOfBuffer)
		{
			AssertRejected<std::string>(ReadError::EndOfBuffer, Bytes({ 0x01, 0x00 }), "untouched");
		}

		TEST_METHOD(Utf8ValidatorAcceptsBoundaries)
		{
			Assert::IsTrue(IsValidUtf8(std::string_view("\x7F")));
			Assert::IsTrue(IsValidUtf8(std::string_view("\xC2\x80")));         // U+0080
			Assert::IsTrue(IsValidUtf8(std::string_view("\xED\x9F\xBF")));     // U+D7FF
			Assert::IsTrue(IsValidUtf8(std::string_view("\xEE\x80\x80")));     // U+E000
			Assert::IsTrue(IsValidUtf8(std::string_view("\xF4\x8F\xBF\xBF"))); // U+10FFFF
			Assert::IsFalse(IsValidUtf8(std::string_view("\xC1\xBF")));
			Assert::IsFalse(IsValidUtf8(std::string_view("\xF5\x80\x80\x80")));
		}
	};

	TEST_CLASS(SequenceTests)
	{
	public:
		TEST_METHOD(EncodingIsCountThenElements)
		{
			const std::vector<std::uint16_t> values{ 0x0102, 0x0304 };
			AssertBytesEqual(Bytes({ 0x02, 0x00, 0x00, 0x00, 0x02, 0x01, 0x04, 0x03 }), Encode(values));
		}

		TEST_METHOD(EmptySequenceRoundTrips)
		{
			AssertBytesEqual(Bytes({ 0x00, 0x00, 0x00, 0x00 }), Encode(std::vector<float>{}));
			Assert::IsTrue(RoundTrip(std::vector<float>{}).empty());
			Assert::IsTrue(RoundTrip(std::vector<std::string>{}).empty());
		}

		TEST_METHOD(SequencesOfEveryKindRoundTrip)
		{
			const std::vector<std::int64_t> integers{ -1, 0, INT64_MAX, INT64_MIN };
			const std::vector<bool> flags{ true, false, true };
			const std::vector<std::string> names{ "a", "", "\xC3\xA9t\xC3\xA9" };
			const std::vector<demo::Vector3> points{ { 1, 2, 3 }, { -4, 5.5f, 0 } };
			const std::vector<std::vector<std::string>> nested{ { "x", "y" }, {}, { "z" } };

			Assert::IsTrue(integers == RoundTrip(integers));
			Assert::IsTrue(flags == RoundTrip(flags));
			Assert::IsTrue(names == RoundTrip(names));
			Assert::IsTrue(points == RoundTrip(points));
			Assert::IsTrue(nested == RoundTrip(nested));
		}

		TEST_METHOD(CountLargerThanTheDataIsRejectedBeforeAllocating)
		{
			// 1000 Vector3 (12 bytes each) announced, 12 bytes present.
			std::vector<std::byte> bytes = Bytes({ 0xE8, 0x03, 0x00, 0x00 });
			bytes.resize(bytes.size() + 12);
			AssertRejected<std::vector<demo::Vector3>>(ReadError::LengthTooLarge, bytes, { { 9, 9, 9 } });

			AssertRejected<std::vector<std::uint32_t>>(ReadError::LengthTooLarge,
				Bytes({ 0xFF, 0xFF, 0xFF, 0xFF }), { 7u });
		}

		TEST_METHOD(CorruptedElementRejectsTheWholeSequence)
		{
			// Second boolean is 0x02: the sequence is refused and the value stays as it was.
			AssertRejected<std::vector<bool>>(ReadError::InvalidBool,
				Bytes({ 0x02, 0x00, 0x00, 0x00, 0x01, 0x02 }), { false });
		}

		TEST_METHOD(TruncatedSequenceIsRejected)
		{
			const std::vector<std::byte> full = Encode(std::vector<std::string>{ "alpha", "beta" });
			for (std::size_t length = 0; length < full.size(); ++length)
			{
				ByteReader reader(std::span<const std::byte>(full.data(), length));
				std::vector<std::string> value{ "sentinel" };
				Assert::IsFalse(SerializationTraits<std::vector<std::string>>::Deserialize(reader, value).IsOk());
				Assert::AreEqual(std::size_t{ 1 }, value.size());
				Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition());
			}
		}

		TEST_METHOD(MinimumEncodedSizesAreComputedFromTheAdapters)
		{
			static_assert(detail::MinEncodedSize<std::uint32_t>() == 4);
			static_assert(detail::MinEncodedSize<bool>() == 1);
			static_assert(detail::MinEncodedSize<std::string>() == 4);
			static_assert(detail::MinEncodedSize<std::vector<double>>() == 4);
			static_assert(detail::MinEncodedSize<demo::Vector3>() == 12);
			static_assert(detail::MinEncodedSize<std::array<demo::Vector3, 2>>() == 24);
		}
	};

	TEST_CLASS(FixedArrayTests)
	{
	public:
		TEST_METHOD(ArrayHasNoPrefixAndRoundTrips)
		{
			const std::array<std::uint8_t, 3> values{ 1, 2, 3 };
			AssertBytesEqual(Bytes({ 1, 2, 3 }), Encode(values));
			Assert::IsTrue(values == RoundTrip(values));
		}

		TEST_METHOD(TruncatedArrayIsRejected)
		{
			AssertRejected<std::array<std::uint16_t, 2>>(ReadError::EndOfBuffer, Bytes({ 1, 0, 2 }), { 5, 6 });
		}
	};
}
