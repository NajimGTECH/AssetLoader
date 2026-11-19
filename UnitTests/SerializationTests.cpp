#include "pch.h"
#include "TestHelpers.h"

#include "Serialization/Deserializer.h"
#include "Serialization/Serializer.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace tests;

namespace SerializationTests
{
	// Compile-time API checks.
	static_assert(!std::is_constructible_v<Deserializer, std::vector<std::byte>&&>,
		"Constructing a deserializer from a temporary vector must not compile (dangling view)");

	/*
	* Serializer / Deserializer tests: each test writes values through the Serializer,
	* reads them back through the Deserializer and checks values and position tracking.
	*/
	TEST_CLASS(SerializerTests)
	{
	public:

		TEST_METHOD(UInt32RoundTrips)
		{
			Serializer serializer;
			serializer.Write(std::uint32_t{ 42 });

			Deserializer deserializer(serializer.GetBuffer());

			std::uint32_t value = 0;
			AssertOk(deserializer.Read(value));
			Assert::AreEqual(std::uint32_t{ 42 }, value);
			Assert::IsTrue(deserializer.IsAtEnd());
		}

		TEST_METHOD(Int32RoundTrips)
		{
			Serializer serializer;
			serializer.Write(std::int32_t{ -123 });

			Deserializer deserializer(serializer.GetBuffer());

			std::int32_t value = 0;
			AssertOk(deserializer.Read(value));
			Assert::AreEqual(std::int32_t{ -123 }, value);
			Assert::IsTrue(deserializer.IsAtEnd());
		}

		TEST_METHOD(FloatRoundTrips)
		{
			Serializer serializer;
			serializer.Write(3.14f);

			Deserializer deserializer(serializer.GetBuffer());

			float value = 0.0f;
			AssertOk(deserializer.Read(value));
			Assert::AreEqual(3.14f, value);
			Assert::IsTrue(deserializer.IsAtEnd());
		}

		TEST_METHOD(BoolRoundTrips)
		{
			Serializer serializer;
			serializer.Write(true);

			Deserializer deserializer(serializer.GetBuffer());

			bool value = false;
			AssertOk(deserializer.Read(value));
			Assert::IsTrue(value);
			Assert::IsTrue(deserializer.IsAtEnd());
		}

		TEST_METHOD(MultipleValuesRoundTripInOrder)
		{
			Serializer serializer;
			serializer.Write(std::uint8_t{ 10 });
			serializer.Write(std::uint32_t{ 250 });
			serializer.Write(false);

			Deserializer deserializer(serializer.GetBuffer());

			std::uint8_t byteValue = 0;
			std::uint32_t integerValue = 0;
			bool boolValue = true;
			AssertOk(deserializer.Read(byteValue), L"Reading the uint8");
			AssertOk(deserializer.Read(integerValue), L"Reading the uint32");
			AssertOk(deserializer.Read(boolValue), L"Reading the bool");

			Assert::AreEqual(std::uint8_t{ 10 }, byteValue);
			Assert::AreEqual(std::uint32_t{ 250 }, integerValue);
			Assert::IsFalse(boolValue);
			Assert::IsTrue(deserializer.IsAtEnd());
		}

		TEST_METHOD(ReadPastEndFailsAndKeepsPosition)
		{
			Serializer serializer;
			serializer.Write(std::uint8_t{ 42 });

			Deserializer deserializer(serializer.GetBuffer());

			std::uint8_t firstValue = 0;
			AssertOk(deserializer.Read(firstValue));
			Assert::AreEqual(std::uint8_t{ 42 }, firstValue);

			std::uint32_t secondValue = 0;
			AssertFails(ReadError::EndOfBuffer, deserializer.Read(secondValue));
			Assert::AreEqual(sizeof(std::uint8_t), deserializer.GetPosition(), L"A failed read must not move the position");
		}
	};
}
