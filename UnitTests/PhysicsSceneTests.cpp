#include "pch.h"
#include "TestHelpers.h"

#include <cmath>
#include <fstream>
#include <limits>
#include <stdexcept>

#include "Archive/ArchiveReader.h"
#include "Archive/ArchiveWriter.h"
#include "Assets/TypedAsset.h"
#include "ByteAccess/ByteReader.h"
#include "ByteAccess/ByteWriter.h"
#include "Physics/World.h"
#include "PhysicsIO/ExampleScene.h"
#include "PhysicsIO/SceneAdapter.h"
#include "PhysicsIO/SceneSerialization.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace physics_io;
using namespace tests;

/*
* Physics integration: capture and restore of the engine's world, scene encoding and versions,
* rebuilding of references and derived data, and rejection of invalid scenes without partial state.
*/
namespace PhysicsSceneTests
{
	namespace
	{
		const AssetId kSceneId = MakeAssetId("scenes/example");
		const AssetId kSnapshotId = MakeAssetId("snapshots/example-step-120");

		SceneRecord ExampleRecord(std::uint32_t steps = 0)
		{
			physics::World world;
			BuildExampleWorld(world);
			for (std::uint32_t i = 0; i < steps; ++i)
			{
				world.Step();
			}
			return CaptureScene(world, "example");
		}

		std::vector<std::byte> Encode(const SceneRecord& scene, SchemaVersion version = kSceneVersion)
		{
			ByteWriter writer;
			WriteScene(writer, scene, version);
			return writer.ReleaseBuffer();
		}

		SceneRecord Decode(std::span<const std::byte> bytes)
		{
			ByteReader reader(bytes);
			SceneRecord scene;
			AssertOk(ReadScene(reader, scene));
			Assert::IsTrue(reader.IsAtEnd());
			return scene;
		}

		void AssertSceneFails(SceneError expected, const SceneResult& result, std::uint32_t objectId = 0)
		{
			Assert::AreEqual(std::string(ToString(expected)), std::string(ToString(result.GetError())));
			Assert::AreEqual(objectId, result.GetObjectId());
		}

		/** Checks that loading `scene` fails with `expected` and leaves a populated world untouched. */
		void AssertRejectedWithoutPartialState(SceneError expected, const SceneRecord& scene, std::uint32_t objectId = 0)
		{
			physics::World world;
			BuildExampleWorld(world);
			world.Step();
			const SceneRecord before = CaptureScene(world, "example");

			AssertSceneFails(expected, ReplaceWorld(scene, world), objectId);
			Assert::IsTrue(before == CaptureScene(world, "example"), L"The world must be left exactly as it was");
		}
	}

	TEST_CLASS(SceneRoundTripTests)
	{
	public:
		TEST_METHOD(SnapshotRecordsTheEndOfTheStep)
		{
			const SceneRecord snapshot = ExampleRecord(120);
			Assert::AreEqual(std::uint64_t{ 120 }, snapshot.stepIndex);
			Assert::AreEqual(2.0, snapshot.time, 1e-4);
			Assert::AreEqual(std::size_t{ 7 }, snapshot.bodies.size());
			Assert::AreEqual(std::size_t{ 1 }, snapshot.joints.size());
		}

		TEST_METHOD(EncodeDecodeGivesTheSameRecord)
		{
			const SceneRecord snapshot = ExampleRecord(120);
			Assert::IsTrue(snapshot == Decode(Encode(snapshot)));
		}

		TEST_METHOD(EncodingStartsWithTheSceneVersion)
		{
			const std::vector<std::byte> bytes = Encode(ExampleRecord());
			AssertBytesEqual(Bytes({ 0x02, 0x00 }), std::span(bytes).first(2));
		}

		TEST_METHOD(EmptySceneRoundTrips)
		{
			physics::World empty;
			const SceneRecord scene = CaptureScene(empty);
			Assert::IsTrue(scene.bodies.empty());
			Assert::IsTrue(scene == Decode(Encode(scene)));

			physics::World restored;
			Assert::IsTrue(LoadIntoEmptyWorld(scene, restored).IsOk());
			Assert::IsTrue(restored.IsEmpty());
		}

		TEST_METHOD(RestoreIntoEmptyWorldGivesTheSameState)
		{
			const SceneRecord snapshot = ExampleRecord(120);

			physics::World restored;
			Assert::IsTrue(LoadIntoEmptyWorld(Decode(Encode(snapshot)), restored).IsOk());
			Assert::IsTrue(snapshot == CaptureScene(restored, "example"));
			Assert::AreEqual(std::uint64_t{ 120 }, restored.GetStepIndex());
		}

		TEST_METHOD(RestoredWorldStepsExactlyLikeTheOriginal)
		{
			physics::World original;
			BuildExampleWorld(original);
			for (int i = 0; i < 90; ++i) { original.Step(); }

			physics::World restored;
			Assert::IsTrue(LoadIntoEmptyWorld(Decode(Encode(CaptureScene(original))), restored).IsOk());

			for (int i = 0; i < 90; ++i)
			{
				original.Step();
				restored.Step();
			}
			Assert::IsTrue(CaptureScene(original) == CaptureScene(restored), L"Same data, same order: bit-identical states");
		}

		TEST_METHOD(ReferencesAndDerivedDataAreRebuilt)
		{
			physics::World original;
			BuildExampleWorld(original);
			for (int i = 0; i < 30; ++i) { original.Step(); }

			physics::World restored;
			Assert::IsTrue(LoadIntoEmptyWorld(Decode(Encode(CaptureScene(original))), restored).IsOk());

			for (const auto& body : restored.GetBodies())
			{
				const physics::RigidBody* source = original.FindBody(body->id);
				Assert::IsNotNull(source);
				Assert::AreEqual(source->colliders.size(), body->colliders.size());
				for (const physics::Collider* collider : body->colliders)
				{
					Assert::IsTrue(collider->body == body.get(), L"Collider -> body pointer rebuilt");
				}
				Assert::AreEqual(source->inverseMass, body->inverseMass);
				Assert::IsTrue(source->inverseWorldInertia == body->inverseWorldInertia);
				Assert::IsTrue(source->bounds == body->bounds);
			}

			const physics::DistanceJoint& joint = *restored.GetJoints()[0];
			Assert::IsTrue(joint.bodyA == restored.FindBody(6), L"Joint -> body pointers rebuilt from IDs");
			Assert::IsTrue(joint.bodyB == restored.FindBody(7));
			Assert::AreEqual(0.0f, restored.FindBody(1)->inverseMass, L"Static body: infinite mass");
			Assert::AreEqual(0.5f, restored.FindBody(2)->inverseMass);
		}

		TEST_METHOD(ReplaceWorldReplacesTheExistingContent)
		{
			physics::World world;
			BuildGridWorld(world, 20);
			Assert::IsNotNull(world.FindBody(21));

			const SceneRecord example = ExampleRecord(10);
			Assert::IsTrue(ReplaceWorld(example, world).IsOk());
			Assert::IsNull(world.FindBody(21), L"Bodies of the previous world are gone");
			Assert::IsTrue(example == CaptureScene(world, "example"));
		}

		TEST_METHOD(LoadIntoEmptyWorldRefusesAPopulatedWorld)
		{
			physics::World world;
			BuildExampleWorld(world);
			const SceneRecord before = CaptureScene(world);
			AssertSceneFails(SceneError::WorldNotEmpty, LoadIntoEmptyWorld(ExampleRecord(5), world));
			Assert::IsTrue(before == CaptureScene(world));
		}
	};

	TEST_CLASS(SceneVersionTests)
	{
	public:
		TEST_METHOD(Version1LoadsWithDefaultDamping)
		{
			const SceneRecord current = ExampleRecord(20);
			const std::vector<std::byte> v1 = Encode(current, 1);
			Assert::IsTrue(v1.size() < Encode(current).size(), L"Version 1 has no damping fields");

			const SceneRecord loaded = Decode(v1);
			Assert::AreEqual(current.bodies.size(), loaded.bodies.size());
			for (std::size_t i = 0; i < loaded.bodies.size(); ++i)
			{
				Assert::AreEqual(0.0f, loaded.bodies[i].linearDamping, L"Added field gets its default value");
				Assert::AreEqual(0.0f, loaded.bodies[i].angularDamping);

				BodyRecord expected = current.bodies[i];
				expected.linearDamping = 0.0f;
				expected.angularDamping = 0.0f;
				Assert::IsTrue(expected == loaded.bodies[i], L"Every other field is preserved");
			}

			physics::World world;
			Assert::IsTrue(LoadIntoEmptyWorld(loaded, world).IsOk());
		}

		TEST_METHOD(UnknownVersionsAreRejected)
		{
			std::vector<std::byte> bytes = Encode(ExampleRecord());
			for (const std::uint16_t version : std::initializer_list<std::uint16_t>{ 0, 3, 100, 65535 })
			{
				bytes[0] = static_cast<std::byte>(version & 0xFF);
				bytes[1] = static_cast<std::byte>(version >> 8);

				ByteReader reader(bytes);
				SceneRecord scene = ExampleRecord(3);
				AssertFails(ReadError::UnsupportedVersion, ReadScene(reader, scene));
				Assert::AreEqual(std::size_t{ 0 }, reader.GetPosition());
				Assert::IsTrue(scene == ExampleRecord(3), L"Output untouched");
			}
		}

		TEST_METHOD(WritingAnUnknownVersionIsRefused)
		{
			ByteWriter writer;
			Assert::ExpectException<std::invalid_argument>([&] { WriteScene(writer, ExampleRecord(), 3); });
			Assert::ExpectException<std::invalid_argument>([&] { WriteScene(writer, ExampleRecord(), 0); });
		}
	};

	TEST_CLASS(SceneValidationTests)
	{
	public:
		TEST_METHOD(EveryTruncationIsRejected)
		{
			const std::vector<std::byte> bytes = Encode(ExampleRecord(5));
			for (std::size_t length = 0; length < bytes.size(); ++length)
			{
				ByteReader reader(std::span(bytes).first(length));
				SceneRecord scene;
				Assert::IsFalse(ReadScene(reader, scene).IsOk());
				Assert::IsTrue(scene.bodies.empty(), L"No partially decoded scene");
			}
		}

		TEST_METHOD(UnknownMotionKindIsRejected)
		{
			std::vector<std::byte> bytes = Encode(ExampleRecord());
			// version(2) + name(4 + 7) + gravity(12) + timestep(4) + stepIndex(8) + time(8) + count(4) + body id(4)
			const std::size_t motionOffset = 2 + 11 + 12 + 4 + 8 + 8 + 4 + 4;
			bytes[motionOffset] = std::byte{ 9 };

			ByteReader reader(bytes);
			SceneRecord scene;
			AssertFails(ReadError::InvalidValue, ReadScene(reader, scene));
		}

		TEST_METHOD(ExcessiveBodyCountIsRejectedBeforeAllocating)
		{
			std::vector<std::byte> bytes = Encode(ExampleRecord());
			const std::size_t countOffset = 2 + 11 + 12 + 4 + 8 + 8;
			for (std::size_t i = 0; i < 4; ++i)
			{
				bytes[countOffset + i] = std::byte{ 0xFF };
			}

			ByteReader reader(bytes);
			SceneRecord scene;
			AssertFails(ReadError::LengthTooLarge, ReadScene(reader, scene));
		}

		TEST_METHOD(InvalidReferenceIsRejected)
		{
			SceneRecord scene = ExampleRecord();
			scene.joints[0].bodyB = 999; // no body with this ID
			AssertSceneFails(SceneError::InvalidReference, ValidateScene(scene), 201);
			AssertRejectedWithoutPartialState(SceneError::InvalidReference, scene, 201);
		}

		TEST_METHOD(SelfReferenceIsRejected)
		{
			SceneRecord scene = ExampleRecord();
			scene.joints[0].bodyB = scene.joints[0].bodyA;
			AssertRejectedWithoutPartialState(SceneError::SelfReference, scene, 201);
		}

		TEST_METHOD(DuplicateIdsAreRejected)
		{
			SceneRecord bodies = ExampleRecord();
			bodies.bodies[3].id = bodies.bodies[1].id;
			AssertRejectedWithoutPartialState(SceneError::DuplicateBodyId, bodies, bodies.bodies[1].id);

			SceneRecord colliders = ExampleRecord();
			colliders.bodies[2].colliders[0].id = 101;
			AssertRejectedWithoutPartialState(SceneError::DuplicateColliderId, colliders, 101);

			SceneRecord joints = ExampleRecord();
			joints.joints.push_back(joints.joints[0]);
			AssertRejectedWithoutPartialState(SceneError::DuplicateJointId, joints, 201);
		}

		TEST_METHOD(ZeroIdsAreRejected)
		{
			SceneRecord scene = ExampleRecord();
			scene.bodies[0].id = 0;
			AssertRejectedWithoutPartialState(SceneError::InvalidBodyId, scene);
		}

		TEST_METHOD(InvalidValuesAreRejected)
		{
			const float nan = std::numeric_limits<float>::quiet_NaN();

			SceneRecord mass = ExampleRecord();
			mass.bodies[1].mass = 0.0f; // dynamic body without mass
			AssertRejectedWithoutPartialState(SceneError::InvalidMass, mass, mass.bodies[1].id);

			SceneRecord inertia = ExampleRecord();
			inertia.bodies[2].inertia.y = -1.0f;
			AssertRejectedWithoutPartialState(SceneError::InvalidMass, inertia, inertia.bodies[2].id);

			SceneRecord position = ExampleRecord();
			position.bodies[1].position.x = nan;
			AssertRejectedWithoutPartialState(SceneError::InvalidBodyState, position, position.bodies[1].id);

			SceneRecord orientation = ExampleRecord();
			orientation.bodies[1].orientation = { 0.0f, 0.0f, 0.0f, 0.0f };
			AssertRejectedWithoutPartialState(SceneError::InvalidOrientation, orientation, orientation.bodies[1].id);

			SceneRecord shape = ExampleRecord();
			shape.bodies[1].colliders[0].radius = -0.5f;
			AssertRejectedWithoutPartialState(SceneError::InvalidShape, shape, shape.bodies[1].colliders[0].id);

			SceneRecord timestep = ExampleRecord();
			timestep.fixedTimestep = 0.0f;
			AssertRejectedWithoutPartialState(SceneError::InvalidSettings, timestep);

			SceneRecord rope = ExampleRecord();
			rope.joints[0].restLength = -1.0f;
			AssertRejectedWithoutPartialState(SceneError::InvalidJoint, rope, 201);
		}
	};

	TEST_CLASS(SceneArchiveTests)
	{
	public:
		TEST_METHOD(SceneAndSnapshotRoundTripThroughAnArchiveFile)
		{
			physics::World world;
			BuildExampleWorld(world);

			ArchiveWriter writer;
			AssertOk(SaveWorld(writer, kSceneId, world, "example"));
			for (int i = 0; i < 120; ++i) { world.Step(); }
			AssertOk(SaveWorld(writer, kSnapshotId, world, "example"));

			TempFile file;
			AssertOk(writer.SaveToFile(file.GetPath()));

			ArchiveReader reader;
			AssertOk(reader.OpenFile(file.GetPath()));

			physics::World restored;
			const WorldLoadResult snapshot = LoadWorld(reader, kSnapshotId, restored);
			const std::string message = snapshot.GetMessage();
			Assert::IsTrue(snapshot.IsOk(), std::wstring(message.begin(), message.end()).c_str());
			Assert::IsTrue(CaptureScene(world, "example") == CaptureScene(restored, "example"));

			Assert::IsTrue(LoadWorld(reader, kSceneId, restored).IsOk());
			Assert::AreEqual(std::uint64_t{ 0 }, restored.GetStepIndex(), L"Back to the authored scene");
		}

		TEST_METHOD(AlteredByteInASavedSceneIsDetected)
		{
			physics::World world;
			BuildExampleWorld(world);
			ArchiveWriter writer;
			AssertOk(SaveWorld(writer, kSceneId, world));
			std::vector<std::byte> bytes = writer.Build();
			bytes[bytes.size() - 10] ^= std::byte{ 0x01 }; // inside the scene payload

			ArchiveReader reader;
			AssertFails(ArchiveError::AssetChecksumMismatch, reader.Open(std::move(bytes)));
		}

		TEST_METHOD(InvalidSceneInAnArchiveLeavesTheWorldUntouched)
		{
			SceneRecord broken = ExampleRecord();
			broken.joints[0].bodyA = 12345;

			ArchiveWriter writer;
			AssertOk(AddTypedAsset(writer, kSceneId, broken)); // valid bytes, invalid content
			ArchiveReader reader;
			AssertOk(reader.Open(writer.Build()));

			physics::World world;
			BuildGridWorld(world, 5);
			const SceneRecord before = CaptureScene(world);

			const WorldLoadResult result = LoadWorld(reader, kSceneId, world);
			Assert::IsFalse(result.IsOk());
			AssertSceneFails(SceneError::InvalidReference, result.sceneResult, 201);
			Assert::IsTrue(before == CaptureScene(world));
		}
	};
}
