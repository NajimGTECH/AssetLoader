#include "pch.h"
#include "TestHelpers.h"

#include "Deserializer.h"
#include "Serializer.h"
#include "SerializationAdapter.h"
#include "Types/RigidBodyDesc.h"
#include "Types/Vector3.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace asset::demo;
using namespace tests;

/*
* A type added by the tests themselves, with a hand-written adapter (custom encoding).
* It proves that new types can be plugged in from outside the AssetEngine project.
*/
namespace SerializationTestTypes
{
	struct Color
	{
		std::uint8_t r = 0;
		std::uint8_t g = 0;
		std::uint8_t b = 0;
		std::uint8_t a = 255;

		[[nodiscard]] friend bool operator==(const Color&, const Color&) = default;
	};

	struct NotRegistered
	{
		int value = 0;
	};
}

namespace asset
{
	// Custom encoding: the four channels packed into a single little-endian uint32 (0xAABBGGRR).
	template<>
	struct SerializationTraits<SerializationTestTypes::Color>
	{
		static void Serialize(ByteWriter& writer, const SerializationTestTypes::Color& color)
		{
			writer.WriteUInt32(static_cast<std::uint32_t>(color.r)
				| (static_cast<std::uint32_t>(color.g) << 8)
				| (static_cast<std::uint32_t>(color.b) << 16)
				| (static_cast<std::uint32_t>(color.a) << 24));
		}

		static ReadResult Deserialize(ByteReader& reader, SerializationTestTypes::Color& color)
		{
			std::uint32_t packed = 0;
			const ReadResult result = reader.ReadUInt32(packed);
			if (result)
			{
				color.r = static_cast<std::uint8_t>(packed & 0xFFu);
				color.g = static_cast<std::uint8_t>((packed >> 8) & 0xFFu);
				color.b = static_cast<std::uint8_t>((packed >> 16) & 0xFFu);
				color.a = static_cast<std::uint8_t>((packed >> 24) & 0xFFu);
			}
			return result;
		}
	};
}

namespace SerializationTests
{
	using SerializationTestTypes::Color;
	using SerializationTestTypes::NotRegistered;

	// The concept is checked at compile time: an unregistered type is rejected before any test runs.
	static_assert(Serializable<std::uint32_t>);
	static_assert(Serializable<bool>);
	static_assert(Serializable<Vector3>);
	static_assert(Serializable<BodyType>);
	static_assert(Serializable<RigidBodyDesc>);
	static_assert(Serializable<Color>);
	static_assert(!Serializable<NotRegistered>);

	TEST_CLASS(StructAdapterTests)
	{
	public:

		TEST_METHOD(Vector3RoundTrip)
		{
			const Vector3 expected{ 1.5f, -2.25f, 1000.0f };

			Serializer serializer;
			serializer.Write(expected);

			Deserializer deserializer(serializer.GetBuffer());
			Vector3 actual{};
			AssertOk(deserializer.Read(actual));

			Assert::IsTrue(expected == actual);
			Assert::IsTrue(deserializer.IsAtEnd());
		}

		TEST_METHOD(Vector3EncodingIsThreeFloatsWithoutHeader)
		{
			Serializer serializer;
			serializer.Write(Vector3{ 1.0f, 2.0f, -1.0f });

			// 1.0f = 0x3F800000, 2.0f = 0x40000000, -1.0f = 0xBF800000, little-endian.
			AssertBytesEqual(
				Bytes({ 0x00, 0x00, 0x80, 0x3F, 0x00, 0x00, 0x00, 0x40, 0x00, 0x00, 0x80, 0xBF }),
				serializer.GetBuffer());
		}

		TEST_METHOD(TruncatedStructFailsWithoutSideEffects)
		{
			Serializer serializer;
			serializer.Write(Vector3{ 1.0f, 2.0f, 3.0f });
			std::vector<std::byte> bytes = serializer.ReleaseBuffer();
			bytes.resize(8); // z is missing

			Deserializer deserializer(bytes);
			Vector3 value{ 9.0f, 9.0f, 9.0f };
			AssertFails(ReadError::EndOfBuffer, deserializer.Read(value));

			// Strong guarantee: the value is untouched (x and y were not partially assigned) and the position did not move.
			Assert::IsTrue(value == Vector3{ 9.0f, 9.0f, 9.0f });
			Assert::AreEqual(std::size_t{ 0 }, deserializer.GetPosition());
		}

		TEST_METHOD(TruncatedStructRestoresPositionWhenUsedThroughTraits)
		{
			const std::vector<std::byte> bytes = Bytes({ 0x00, 0x00, 0x80, 0x3F, 0x00 });
			ByteReader reader(bytes);

			Vector3 value{};
			AssertFails(ReadError::EndOfBuffer, SerializationTraits<Vector3>::Deserialize(reader, value));
			Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition());
		}

		TEST_METHOD(StructsCanBeMixedWithPrimitives)
		{
			Serializer serializer;
			serializer.Write(std::uint8_t{ 7 });
			serializer.Write(Vector3{ 1.0f, 2.0f, 3.0f });
			serializer.Write(true);

			Deserializer deserializer(serializer.GetBuffer());
			std::uint8_t first = 0;
			Vector3 second{};
			bool third = false;

			AssertOk(deserializer.Read(first));
			AssertOk(deserializer.Read(second));
			AssertOk(deserializer.Read(third));

			Assert::AreEqual(std::uint8_t{ 7 }, first);
			Assert::IsTrue(second == Vector3{ 1.0f, 2.0f, 3.0f });
			Assert::IsTrue(third);
			Assert::IsTrue(deserializer.IsAtEnd());
		}
	};

	TEST_CLASS(EnumAdapterTests)
	{
	public:

		TEST_METHOD(EnumIsStoredAsUnderlyingType)
		{
			Serializer serializer;
			serializer.Write(BodyType::Kinematic);

			AssertBytesEqual(Bytes({ 0x02 }), serializer.GetBuffer());
		}

		TEST_METHOD(EnumRoundTrip)
		{
			for (const BodyType expected : { BodyType::Static, BodyType::Dynamic, BodyType::Kinematic })
			{
				Serializer serializer;
				serializer.Write(expected);

				Deserializer deserializer(serializer.GetBuffer());
				BodyType actual = BodyType::Dynamic;
				AssertOk(deserializer.Read(actual));
				Assert::IsTrue(expected == actual);
			}
		}

		TEST_METHOD(UnknownEnumValueIsRejected)
		{
			const std::vector<std::byte> bytes = Bytes({ 0x03 });
			Deserializer deserializer(bytes);

			BodyType value = BodyType::Dynamic;
			AssertFails(ReadError::InvalidValue, deserializer.Read(value));

			Assert::IsTrue(value == BodyType::Dynamic);
			Assert::AreEqual(std::size_t{ 0 }, deserializer.GetPosition());
		}
	};

	TEST_CLASS(CustomAdapterTests)
	{
	public:

		TEST_METHOD(HandWrittenAdapterUsesItsOwnEncoding)
		{
			Serializer serializer;
			serializer.Write(Color{ 0x11, 0x22, 0x33, 0x44 });

			AssertBytesEqual(Bytes({ 0x11, 0x22, 0x33, 0x44 }), serializer.GetBuffer());
		}

		TEST_METHOD(HandWrittenAdapterRoundTrip)
		{
			const Color expected{ 10, 20, 30, 40 };

			Serializer serializer;
			serializer.Write(expected);

			Deserializer deserializer(serializer.GetBuffer());
			Color actual{};
			AssertOk(deserializer.Read(actual));
			Assert::IsTrue(expected == actual);
		}
	};
}
