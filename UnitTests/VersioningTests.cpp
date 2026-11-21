#include "pch.h"
#include "TestHelpers.h"

#include "Deserializer.h"
#include "Serializer.h"
#include "Types/RigidBodyDesc.h"
#include "Types/Vector3.h"
#include "Versioning.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace asset::demo;
using namespace tests;

namespace VersioningTests
{
	namespace
	{
		using RigidBodyTraits = SerializationTraits<RigidBodyDesc>;

		void WriteVector3(ByteWriter& writer, const Vector3& value)
		{
			writer.WriteFloat(value.x);
			writer.WriteFloat(value.y);
			writer.WriteFloat(value.z);
		}

		/*
		* Builds a RigidBodyDesc exactly as a V1 build of the engine wrote it, with the Byte Access
		* Layer only. This pins down the V1 format independently of the current code.
		*/
		std::vector<std::byte> MakeV1Body(std::uint32_t id, float mass, const Vector3& position, const Vector3& velocity)
		{
			ByteWriter writer;
			writer.WriteUInt16(1);
			writer.WriteUInt32(id);
			writer.WriteFloat(mass);
			WriteVector3(writer, position);
			WriteVector3(writer, velocity);
			return writer.ReleaseBuffer();
		}

		std::vector<std::byte> WithVersion(std::vector<std::byte> bytes, std::uint16_t version)
		{
			bytes[0] = static_cast<std::byte>(version & 0xFFu);
			bytes[1] = static_cast<std::byte>(version >> 8);
			return bytes;
		}

		RigidBodyDesc MakeBody()
		{
			RigidBodyDesc body;
			body.id = 42;
			body.mass = 12.5f;
			body.position = { 1.0f, 2.0f, 3.0f };
			body.velocity = { -4.0f, 0.0f, 9.81f };
			body.type = BodyType::Kinematic;
			body.friction = 0.8f;
			body.restitution = 0.25f;
			return body;
		}
	}

	TEST_CLASS(SchemaVersionTests)
	{
	public:

		TEST_METHOD(CurrentVersionIsWrittenFirst)
		{
			Serializer serializer;
			serializer.Write(MakeBody());

			const std::span<const std::byte> buffer = serializer.GetBuffer();
			Assert::IsTrue(buffer.size() >= 2);
			AssertBytesEqual(Bytes({ 0x02, 0x00 }), buffer.first(2));
		}

		TEST_METHOD(CurrentVersionHasExpectedSize)
		{
			Serializer serializer;
			serializer.Write(MakeBody());

			// version 2 + id 4 + mass 4 + position 12 + velocity 12 + type 1 + friction 4 + restitution 4
			Assert::AreEqual(std::size_t{ 43 }, serializer.GetSize());
		}

		TEST_METHOD(CurrentVersionRoundTrip)
		{
			const RigidBodyDesc expected = MakeBody();

			Serializer serializer;
			serializer.Write(expected);

			Deserializer deserializer(serializer.GetBuffer());
			RigidBodyDesc actual{};
			AssertOk(deserializer.Read(actual));

			Assert::IsTrue(expected == actual);
			Assert::IsTrue(deserializer.IsAtEnd());
		}

		TEST_METHOD(SupportedVersionRange)
		{
			Assert::IsFalse(RigidBodyTraits::IsSupported(0));
			Assert::IsTrue(RigidBodyTraits::IsSupported(1));
			Assert::IsTrue(RigidBodyTraits::IsSupported(2));
			Assert::IsFalse(RigidBodyTraits::IsSupported(3));
		}
	};

	TEST_CLASS(MigrationTests)
	{
	public:

		TEST_METHOD(V1IsReadAndNewFieldsGetTheirDefaultValue)
		{
			const std::vector<std::byte> bytes = MakeV1Body(7, 3.0f, { 1.0f, 2.0f, 3.0f }, { 0.0f, -1.0f, 0.0f });
			Assert::AreEqual(std::size_t{ 34 }, bytes.size());

			Deserializer deserializer(bytes);
			RigidBodyDesc body = MakeBody(); // non-default content, must be fully replaced
			AssertOk(deserializer.Read(body));

			Assert::AreEqual(std::uint32_t{ 7 }, body.id);
			Assert::AreEqual(3.0f, body.mass);
			Assert::IsTrue(body.position == Vector3{ 1.0f, 2.0f, 3.0f });
			Assert::IsTrue(body.velocity == Vector3{ 0.0f, -1.0f, 0.0f });

			const RigidBodyDesc defaults{};
			Assert::AreEqual(defaults.friction, body.friction);
			Assert::AreEqual(defaults.restitution, body.restitution);
			Assert::IsTrue(deserializer.IsAtEnd());
		}

		TEST_METHOD(V1BodyWithMassIsMigratedToDynamic)
		{
			const std::vector<std::byte> bytes = MakeV1Body(1, 5.0f, {}, {});

			Deserializer deserializer(bytes);
			RigidBodyDesc body{};
			body.type = BodyType::Kinematic;
			AssertOk(deserializer.Read(body));

			Assert::IsTrue(body.type == BodyType::Dynamic);
		}

		TEST_METHOD(V1BodyWithoutMassIsMigratedToStatic)
		{
			const std::vector<std::byte> bytes = MakeV1Body(2, 0.0f, {}, {});

			Deserializer deserializer(bytes);
			RigidBodyDesc body{};
			AssertOk(deserializer.Read(body));

			Assert::IsTrue(body.type == BodyType::Static);
		}

		TEST_METHOD(MigrationDoesNotRunOnCurrentVersion)
		{
			RigidBodyDesc expected = MakeBody();
			expected.mass = 0.0f;
			expected.type = BodyType::Dynamic; // would become Static if Migrate() ran

			Serializer serializer;
			serializer.Write(expected);

			Deserializer deserializer(serializer.GetBuffer());
			RigidBodyDesc actual{};
			AssertOk(deserializer.Read(actual));

			Assert::IsTrue(actual.type == BodyType::Dynamic);
		}

		TEST_METHOD(V1AndV2CanFollowEachOtherInTheSameBuffer)
		{
			ByteWriter writer;
			writer.WriteBytes(MakeV1Body(1, 0.0f, {}, {}));

			Serializer serializer;
			serializer.Write(MakeBody());
			writer.WriteBytes(serializer.GetBuffer());

			Deserializer deserializer(writer.GetBuffer());
			RigidBodyDesc first{};
			RigidBodyDesc second{};
			AssertOk(deserializer.Read(first));
			AssertOk(deserializer.Read(second));

			Assert::IsTrue(first.type == BodyType::Static);
			Assert::IsTrue(second == MakeBody());
			Assert::IsTrue(deserializer.IsAtEnd());
		}
	};

	TEST_CLASS(UnsupportedVersionTests)
	{
	public:

		TEST_METHOD(NewerVersionIsRejected)
		{
			Serializer serializer;
			serializer.Write(MakeBody());
			const std::vector<std::byte> bytes = WithVersion(serializer.ReleaseBuffer(), 3);

			Deserializer deserializer(bytes);
			RigidBodyDesc value{};
			AssertFails(ReadError::UnsupportedVersion, deserializer.Read(value));

			Assert::IsTrue(value == RigidBodyDesc{});
			Assert::AreEqual(std::size_t{ 0 }, deserializer.GetPosition());
		}

		TEST_METHOD(VersionZeroIsRejected)
		{
			const std::vector<std::byte> bytes = WithVersion(MakeV1Body(1, 1.0f, {}, {}), 0);

			Deserializer deserializer(bytes);
			RigidBodyDesc value{};
			AssertFails(ReadError::UnsupportedVersion, deserializer.Read(value));
			Assert::AreEqual(std::size_t{ 0 }, deserializer.GetPosition());
		}

		TEST_METHOD(RejectionWorksThroughTraitsToo)
		{
			const std::vector<std::byte> bytes = WithVersion(MakeV1Body(1, 1.0f, {}, {}), 0xFFFF);
			ByteReader reader(bytes);

			RigidBodyDesc value{};
			AssertFails(ReadError::UnsupportedVersion, RigidBodyTraits::Deserialize(reader, value));
			Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition());
		}

		TEST_METHOD(MissingVersionHeaderFails)
		{
			const std::vector<std::byte> bytes = Bytes({ 0x02 });

			Deserializer deserializer(bytes);
			RigidBodyDesc value{};
			AssertFails(ReadError::EndOfBuffer, deserializer.Read(value));
			Assert::AreEqual(std::size_t{ 0 }, deserializer.GetPosition());
		}

		TEST_METHOD(TruncatedCurrentVersionFailsWithoutSideEffects)
		{
			Serializer serializer;
			serializer.Write(MakeBody());
			std::vector<std::byte> bytes = serializer.ReleaseBuffer();
			bytes.pop_back(); // restitution is incomplete

			Deserializer deserializer(bytes);
			RigidBodyDesc value{};
			AssertFails(ReadError::EndOfBuffer, deserializer.Read(value));

			Assert::IsTrue(value == RigidBodyDesc{});
			Assert::AreEqual(std::size_t{ 0 }, deserializer.GetPosition());
		}

		TEST_METHOD(InvalidEnumInsideVersionedStructFails)
		{
			Serializer serializer;
			serializer.Write(MakeBody());
			std::vector<std::byte> bytes = serializer.ReleaseBuffer();
			bytes[34] = std::byte{ 0x7F }; // type: offset 2 + 4 + 4 + 12 + 12

			Deserializer deserializer(bytes);
			RigidBodyDesc value{};
			AssertFails(ReadError::InvalidValue, deserializer.Read(value));
			Assert::AreEqual(std::size_t{ 0 }, deserializer.GetPosition());
		}
	};
}
