/**
 * @file SceneDemo.cpp
 * @brief Physics integration and general-purpose serialization: save the example scene and a
 *        snapshot with typed assets, load them back by ID, restore worlds, and show that invalid
 *        scenes are refused without touching the world.
 */
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <format>
#include <iostream>
#include <limits>
#include <span>
#include <string>
#include <string_view>
#include <vector>

#include "Archive/ArchiveReader.h"
#include "Archive/ArchiveWriter.h"
#include "Assets/TypedAsset.h"
#include "ByteAccess/ByteReader.h"
#include "ByteAccess/ByteWriter.h"
#include "Commands.h"
#include "Physics/World.h"
#include "PhysicsIO/ExampleScene.h"
#include "PhysicsIO/SceneAdapter.h"
#include "PhysicsIO/SceneSerialization.h"
#include "Types/GameSettings.h"
#include "Types/PlayerProfile.h"

namespace app
{
	namespace
	{
		const asset::AssetId kSceneId = asset::MakeAssetId("scenes/example");
		const asset::AssetId kSnapshotId = asset::MakeAssetId("snapshots/example-step-120");
		const asset::AssetId kProfileId = asset::MakeAssetId("saves/profile.bin");
		const asset::AssetId kSettingsId = asset::MakeAssetId("config/settings.bin");
		constexpr int kSnapshotStep = 120;

		int g_failures = 0;

		void PrintTitle(std::string_view title)
		{
			std::cout << "\n=== " << title << " ===\n";
		}

		void Report(std::string_view step, bool ok, std::string_view detail)
		{
			if (!ok)
			{
				++g_failures;
			}
			std::cout << std::format("  {:<50} -> {}{}\n", step, detail, ok ? "" : "   [UNEXPECTED]");
		}

		void ReportScene(std::string_view step, const physics_io::SceneResult& result, physics_io::SceneError expected)
		{
			Report(step, result.GetError() == expected, result.GetMessage());
		}

		asset::demo::PlayerProfile MakeProfile()
		{
			asset::demo::PlayerProfile profile;
			profile.displayName = "L\xC3\xA9na";
			profile.level = 42;
			profile.experience = 1'234'567;
			profile.inventory = { { 1001, "Iron sword", 1, true }, { 2040, "Health potion", 12, false } };
			profile.title = "Dragon slayer";
			profile.achievements = { "first_blood", "explorer" };
			return profile;
		}

		asset::demo::GameSettings MakeSettings()
		{
			asset::demo::GameSettings settings;
			settings.language = "fr-FR";
			settings.windowMode = asset::demo::WindowMode::Borderless;
			settings.resolution = { 2560, 1440 };
			settings.keyBindings = { { "jump", 32, 0 }, { "fire", 1, 17 } };
			return settings;
		}

		/** Adds a scene to an archive, encoded with an explicit scene version (for the v1 / v2 samples). */
		bool AddSceneVersion(asset::ArchiveWriter& writer, asset::AssetId id, const physics_io::SceneRecord& scene,
			asset::SchemaVersion version)
		{
			asset::ByteWriter payload;
			payload.WriteUInt32(asset::AssetTypeInfo<physics_io::SceneRecord>::Tag);
			physics_io::WriteScene(payload, scene, version);
			return writer.AddAsset(id, asset::AssetType::Scene, payload.ReleaseBuffer()).IsOk();
		}

		void PrintBodies(const physics::World& world)
		{
			std::cout << std::format("    {:>3}  {:<9} {:>24}  {:>22}\n", "ID", "Motion", "Position (m)", "Velocity (m/s)");
			for (const auto& body : world.GetBodies())
			{
				const char* motion = body->motion == physics::MotionType::Static ? "static"
					: body->motion == physics::MotionType::Kinematic ? "kinematic" : "dynamic";
				std::cout << std::format("    {:>3}  {:<9} ({:>6.2f}, {:>6.2f}, {:>6.2f})  ({:>5.2f}, {:>5.2f}, {:>5.2f})\n",
					body->id, motion, body->position.x, body->position.y, body->position.z,
					body->linearVelocity.x, body->linearVelocity.y, body->linearVelocity.z);
			}
		}

		std::string Describe(const physics::World& world)
		{
			return std::format("step {}, t = {:.3f} s, {} bodies, {} colliders, {} joints",
				world.GetStepIndex(), world.GetTime(), world.GetBodies().size(), world.GetColliderCount(),
				world.GetJoints().size());
		}

		bool OpenArchive(asset::ArchiveReader& reader, const std::filesystem::path& path)
		{
			if (const asset::ArchiveResult result = reader.OpenFile(path); !result)
			{
				std::cout << "Cannot open " << path.string() << ": " << asset::ToString(result.GetError()) << '\n';
				return false;
			}
			return true;
		}
	}

	int RunSceneDemo(const std::filesystem::path& archivePath)
	{
		g_failures = 0;

		PrintTitle("1. Build the example scene and save it with a snapshot");
		physics::World original;
		physics_io::BuildExampleWorld(original);
		std::cout << "  Example world: " << Describe(original) << '\n';

		asset::ArchiveWriter writer;
		Report("Save the scene (step 0)", physics_io::SaveWorld(writer, kSceneId, original, "example").IsOk(), "saved");
		for (int i = 0; i < kSnapshotStep; ++i)
		{
			original.Step();
		}
		Report(std::format("Save a snapshot at the end of step {}", kSnapshotStep),
			physics_io::SaveWorld(writer, kSnapshotId, original, "example").IsOk(), "saved");
		Report("Save a player profile (general-purpose type)",
			asset::AddTypedAsset(writer, kProfileId, MakeProfile()).IsOk(), "saved");
		Report("Save the game settings (general-purpose type)",
			asset::AddTypedAsset(writer, kSettingsId, MakeSettings()).IsOk(), "saved");

		if (const asset::ArchiveResult result = writer.SaveToFile(archivePath); !result)
		{
			std::cout << "Cannot write " << archivePath.string() << ": " << asset::ToString(result.GetError()) << '\n';
			return 1;
		}
		std::cout << "  Archive written to " << archivePath.string() << '\n';

		PrintTitle("2. Open the archive");
		asset::ArchiveReader reader;
		if (!OpenArchive(reader, archivePath))
		{
			return 1;
		}
		PrintDirectory(reader);

		PrintTitle("3. Load general-purpose assets by ID (only their bytes are decoded)");
		asset::demo::PlayerProfile profile;
		const asset::AssetLoadResult profileResult = asset::LoadTypedAsset(reader, kProfileId, profile);
		Report("Load the player profile", profileResult.IsOk() && profile == MakeProfile(),
			std::format("{}, level {}, {} items, {} achievements", profile.displayName, profile.level,
				profile.inventory.size(), profile.achievements.size()));

		asset::demo::GameSettings settings;
		const asset::AssetLoadResult settingsResult = asset::LoadTypedAsset(reader, kSettingsId, settings);
		Report("Load the game settings", settingsResult.IsOk() && settings == MakeSettings(),
			std::format("{}, {}x{}, {} key bindings", settings.language, settings.resolution[0],
				settings.resolution[1], settings.keyBindings.size()));

		Report("Load the profile as GameSettings (wrong type)",
			asset::LoadTypedAsset(reader, kProfileId, settings).GetError() == asset::AssetLoadError::TagMismatch,
			asset::ToString(asset::AssetLoadError::TagMismatch));

		PrintTitle("4. Restore the snapshot into an empty world");
		physics::World restored;
		physics_io::SceneRecord snapshot;
		const bool snapshotDecoded = asset::LoadTypedAsset(reader, kSnapshotId, snapshot).IsOk();
		Report("Decode the snapshot", snapshotDecoded,
			std::format("{} bodies, {} joints", snapshot.bodies.size(), snapshot.joints.size()));
		ReportScene("Load it into an empty world", physics_io::LoadIntoEmptyWorld(snapshot, restored), physics_io::SceneError::None);
		std::cout << "  Restored world: " << Describe(restored) << '\n';
		PrintBodies(restored);
		Report("Restored state equals the saved state",
			physics_io::CaptureScene(restored, "example") == physics_io::CaptureScene(original, "example"), "identical");

		const physics::DistanceJoint& rope = *restored.GetJoints()[0];
		Report("Joint references rebuilt from body IDs",
			rope.bodyA == restored.FindBody(6) && rope.bodyB == restored.FindBody(7), "joint 201 -> bodies 6 and 7");

		PrintTitle("5. Continue both simulations for 60 steps");
		for (int i = 0; i < 60; ++i)
		{
			original.Step();
			restored.Step();
		}
		Report("Original and restored worlds after 60 more steps",
			physics_io::CaptureScene(original) == physics_io::CaptureScene(restored), "bit-identical");

		PrintTitle("6. Replace an existing world with the authored scene");
		physics::World existing;
		physics_io::BuildGridWorld(existing, 50);
		std::cout << "  Before: " << Describe(existing) << '\n';
		const physics_io::WorldLoadResult replaced = physics_io::LoadWorld(reader, kSceneId, existing);
		Report("Replace it with scenes/example", replaced.IsOk(), replaced.GetMessage());
		std::cout << "  After:  " << Describe(existing) << '\n';

		PrintTitle("7. Invalid scenes are refused and the world is left untouched");
		const physics_io::SceneRecord before = physics_io::CaptureScene(restored);

		physics_io::SceneRecord broken = snapshot;
		broken.joints[0].bodyB = 999;
		ReportScene("Joint referencing a missing body", physics_io::ReplaceWorld(broken, restored),
			physics_io::SceneError::InvalidReference);

		broken = snapshot;
		broken.bodies[2].id = broken.bodies[1].id;
		ReportScene("Two bodies with the same ID", physics_io::ReplaceWorld(broken, restored),
			physics_io::SceneError::DuplicateBodyId);

		broken = snapshot;
		broken.bodies[1].position.y = std::numeric_limits<float>::quiet_NaN();
		ReportScene("Position is NaN", physics_io::ReplaceWorld(broken, restored), physics_io::SceneError::InvalidBodyState);

		broken = snapshot;
		broken.bodies[1].mass = 0.0f;
		ReportScene("Dynamic body without mass", physics_io::ReplaceWorld(broken, restored), physics_io::SceneError::InvalidMass);

		ReportScene("Load into a world that is not empty", physics_io::LoadIntoEmptyWorld(snapshot, restored),
			physics_io::SceneError::WorldNotEmpty);

		Report("World after the refused loads", physics_io::CaptureScene(restored) == before, "unchanged");

		PrintTitle("8. Scene format versions");
		{
			asset::ByteWriter v1;
			physics_io::WriteScene(v1, snapshot, 1);
			asset::ByteReader v1Reader(v1.GetBuffer());
			physics_io::SceneRecord fromV1;
			const asset::ReadResult v1Result = physics_io::ReadScene(v1Reader, fromV1);
			Report("Read a version-1 scene (no damping fields)",
				v1Result.IsOk() && fromV1.bodies[2].linearDamping == 0.0f,
				std::format("{} bytes, damping of body 3 = {} (default)", v1.GetSize(), fromV1.bodies[2].linearDamping));

			asset::ByteWriter v2;
			physics_io::WriteScene(v2, snapshot, physics_io::kSceneVersion);
			std::vector<std::byte> unknown = v2.ReleaseBuffer();
			unknown[0] = std::byte{ 3 };
			asset::ByteReader unknownReader(unknown);
			physics_io::SceneRecord ignored;
			const asset::ReadResult unknownResult = physics_io::ReadScene(unknownReader, ignored);
			Report("Read a scene with version 3", unknownResult.GetError() == asset::ReadError::UnsupportedVersion,
				asset::ToString(unknownResult.GetError()));
		}

		std::cout << '\n' << (g_failures == 0
			? "Every step gave the expected result.\n"
			: std::format("{} step(s) did not give the expected result.\n", g_failures));
		return g_failures == 0 ? 0 : 1;
	}

	int RunSceneSamples(const std::filesystem::path& directory)
	{
		physics::World world;
		physics_io::BuildExampleWorld(world);
		const physics_io::SceneRecord scene = physics_io::CaptureScene(world, "example");
		for (int i = 0; i < kSnapshotStep; ++i)
		{
			world.Step();
		}
		const physics_io::SceneRecord snapshot = physics_io::CaptureScene(world, "example");

		// The example scene and its snapshot in both scene versions.
		for (asset::SchemaVersion version = physics_io::kOldestSceneVersion; version <= physics_io::kSceneVersion; ++version)
		{
			asset::ArchiveWriter writer;
			if (!AddSceneVersion(writer, kSceneId, scene, version) || !AddSceneVersion(writer, kSnapshotId, snapshot, version))
			{
				std::cout << "Cannot encode the example scene\n";
				return 1;
			}

			const std::filesystem::path path = directory / std::format("scene_v{}.aset", version);
			if (const asset::ArchiveResult result = writer.SaveToFile(path); !result)
			{
				std::cout << "Cannot write " << path.string() << ": " << asset::ToString(result.GetError()) << '\n';
				return 1;
			}
			std::cout << "Wrote " << path.string() << " (scene format version " << version << ")\n";
		}

		// The two general-purpose types.
		asset::ArchiveWriter general;
		if (!asset::AddTypedAsset(general, kProfileId, MakeProfile()) || !asset::AddTypedAsset(general, kSettingsId, MakeSettings()))
		{
			std::cout << "Cannot encode the general-purpose samples\n";
			return 1;
		}
		const std::filesystem::path generalPath = directory / "general.aset";
		if (const asset::ArchiveResult result = general.SaveToFile(generalPath); !result)
		{
			std::cout << "Cannot write " << generalPath.string() << ": " << asset::ToString(result.GetError()) << '\n';
			return 1;
		}
		std::cout << "Wrote " << generalPath.string() << '\n';
		return 0;
	}

	int RunRestore(const std::filesystem::path& archivePath)
	{
		asset::ArchiveReader reader;
		if (!OpenArchive(reader, archivePath))
		{
			return 1;
		}
		PrintDirectory(reader);
		std::cout << '\n';

		int failures = 0;
		for (const asset::AssetEntry& entry : reader.GetEntries())
		{
			std::span<const std::byte> payload;
			static_cast<void>(reader.GetAssetData(entry.id, payload)); // the ID comes from the directory

			std::uint32_t tag = 0;
			asset::ByteReader tagReader(payload);
			static_cast<void>(tagReader.ReadUInt32(tag)); // tag stays 0 for payloads shorter than 4 bytes

			const std::string label = std::format("{:016X}", entry.id);
			if (tag == asset::AssetTypeInfo<physics_io::SceneRecord>::Tag)
			{
				physics::World world;
				const physics_io::WorldLoadResult result = physics_io::LoadWorld(reader, entry.id, world);
				failures += result.IsOk() ? 0 : 1;
				std::cout << std::format("  {} scene    -> {}\n", label,
					result.IsOk() ? "restored: " + Describe(world) : result.GetMessage());
			}
			else if (tag == asset::AssetTypeInfo<asset::demo::PlayerProfile>::Tag)
			{
				asset::demo::PlayerProfile profile;
				const asset::AssetLoadResult result = asset::DecodeTypedAsset(payload, profile);
				failures += result.IsOk() ? 0 : 1;
				std::cout << std::format("  {} profile  -> {}\n", label, result.IsOk()
					? std::format("{} \"{}\", level {}, {} items", profile.displayName, profile.title, profile.level, profile.inventory.size())
					: std::string(result.GetMessage()));
			}
			else if (tag == asset::AssetTypeInfo<asset::demo::GameSettings>::Tag)
			{
				asset::demo::GameSettings settings;
				const asset::AssetLoadResult result = asset::DecodeTypedAsset(payload, settings);
				failures += result.IsOk() ? 0 : 1;
				std::cout << std::format("  {} settings -> {}\n", label, result.IsOk()
					? std::format("{}, {}x{}, {} key bindings", settings.language, settings.resolution[0], settings.resolution[1], settings.keyBindings.size())
					: std::string(result.GetMessage()));
			}
			else
			{
				std::cout << std::format("  {} raw      -> {} bytes ({})\n", label, payload.size(), asset::ToString(entry.type));
			}
		}
		return failures == 0 ? 0 : 1;
	}
}
