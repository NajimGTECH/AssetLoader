#include "pch.h"
#include "TestHelpers.h"

#include "Archive/ArchiveReader.h"
#include "Archive/ArchiveWriter.h"
#include "Assets/TypedAsset.h"
#include "ByteAccess/ByteWriter.h"
#include "Serialization/Deserializer.h"
#include "Serialization/Serializer.h"
#include "Types/GameSettings.h"
#include "Types/PlayerProfile.h"

using namespace Microsoft::VisualStudio::CppUnitTestFramework;
using namespace asset;
using namespace asset::demo;
using namespace tests;

/*
* Round-trips of the two general-purpose types (unrelated to physics), on their own and stored in
* an archive as typed assets, plus schema migration of PlayerProfile and the typed-asset checks.
*/
namespace GeneralPurposeTests
{
	namespace
	{
		const AssetId kProfileId = MakeAssetId("saves/profile.bin");
		const AssetId kSettingsId = MakeAssetId("config/settings.bin");

		PlayerProfile MakeProfile()
		{
			PlayerProfile profile;
			profile.displayName = "L\xC3\xA9na";
			profile.level = 42;
			profile.experience = 1'234'567'890'123ull;
			profile.inventory = {
				{ 1001, "Iron sword", 1, true },
				{ 2040, "Health potion", 12, false },
				{ 3300, "\xE5\x9C\xB0\xE5\x9B\xB3" /* 地図 */, 1, false },
			};
			profile.title = "Dragon slayer";
			profile.achievements = { "first_blood", "explorer", "" };
			return profile;
		}

		GameSettings MakeSettings()
		{
			GameSettings settings;
			settings.language = "fr-FR";
			settings.windowMode = WindowMode::Borderless;
			settings.resolution = { 2560, 1440 };
			settings.vsync = false;
			settings.volumes = { 0.9f, 0.25f, 1.0f };
			settings.keyBindings = { { "jump", 32, 0 }, { "fire", 1, 17 }, { "inventory", 'I', 9 } };
			return settings;
		}

		template<typename T>
		T RoundTrip(const T& value)
		{
			Serializer serializer;
			serializer.Write(value);
			const std::vector<std::byte> bytes = serializer.ReleaseBuffer();

			Deserializer deserializer(bytes);
			T decoded{};
			AssertOk(deserializer.Read(decoded));
			Assert::IsTrue(deserializer.IsAtEnd());
			return decoded;
		}

		void AssertLoadFails(AssetLoadError expected, const AssetLoadResult& result)
		{
			Assert::AreEqual(ToString(expected), ToString(result.GetError()));
		}
	}

	TEST_CLASS(GeneralPurposeRoundTripTests)
	{
	public:
		TEST_METHOD(PlayerProfileRoundTrips)
		{
			const PlayerProfile profile = MakeProfile();
			Assert::IsTrue(profile == RoundTrip(profile));
		}

		TEST_METHOD(EmptyPlayerProfileRoundTrips)
		{
			const PlayerProfile empty{};
			const PlayerProfile decoded = RoundTrip(empty);
			Assert::IsTrue(empty == decoded);
			Assert::IsTrue(decoded.inventory.empty());
			Assert::IsTrue(decoded.achievements.empty());
		}

		TEST_METHOD(GameSettingsRoundTrips)
		{
			const GameSettings settings = MakeSettings();
			Assert::IsTrue(settings == RoundTrip(settings));
			Assert::IsTrue(GameSettings{} == RoundTrip(GameSettings{}));
		}

		TEST_METHOD(UnknownWindowModeIsRejected)
		{
			Serializer serializer;
			serializer.Write(MakeSettings());
			std::vector<std::byte> bytes = serializer.ReleaseBuffer();
			// windowMode is right after the language string (4-byte length + 5 bytes "fr-FR").
			bytes[4 + 5] = std::byte{ 7 };

			Deserializer deserializer(bytes);
			GameSettings decoded{};
			AssertFails(ReadError::InvalidValue, deserializer.Read(decoded));
			Assert::IsTrue(decoded == GameSettings{});
		}

		TEST_METHOD(ProfileV1LoadsWithDefaultValuesForAddedFields)
		{
			// Hand-written V1 encoding: version, displayName, level, experience, inventory (1 item).
			ByteWriter writer;
			writer.WriteUInt16(1);
			writer.WriteUInt32(3);
			writer.WriteBytes(Bytes({ 'B', 'o', 'b' }));
			writer.WriteUInt32(7);
			writer.WriteUInt64(900);
			writer.WriteUInt32(1);
			writer.WriteUInt32(1001);
			writer.WriteUInt32(5);
			writer.WriteBytes(Bytes({ 'S', 'w', 'o', 'r', 'd' }));
			writer.WriteUInt16(1);
			writer.WriteBool(true);

			Deserializer deserializer(writer.GetBuffer());
			PlayerProfile profile{};
			AssertOk(deserializer.Read(profile));
			Assert::IsTrue(deserializer.IsAtEnd());

			Assert::AreEqual(std::string("Bob"), profile.displayName);
			Assert::AreEqual(7u, profile.level);
			Assert::AreEqual(std::size_t{ 1 }, profile.inventory.size());
			Assert::AreEqual(std::string("Sword"), profile.inventory[0].name);
			Assert::AreEqual(std::string("Novice"), profile.title, L"Field added in V2 gets its default value");
			Assert::IsTrue(profile.achievements.empty(), L"Field added in V2 gets its default value");
		}

		TEST_METHOD(UnknownProfileVersionIsRejected)
		{
			ByteWriter writer;
			writer.WriteUInt16(3);
			writer.WriteUInt32(0);

			Deserializer deserializer(writer.GetBuffer());
			PlayerProfile profile = MakeProfile();
			AssertFails(ReadError::UnsupportedVersion, deserializer.Read(profile));
			Assert::IsTrue(profile == MakeProfile());
		}
	};

	TEST_CLASS(TypedAssetTests)
	{
	public:
		TEST_METHOD(TypedAssetsRoundTripThroughAnArchive)
		{
			ArchiveWriter writer;
			AssertOk(AddTypedAsset(writer, kProfileId, MakeProfile()));
			AssertOk(AddTypedAsset(writer, kSettingsId, MakeSettings()));
			AssertOk(writer.AddAsset(MakeAssetId("readme.txt"), AssetType::Text, Bytes({ 'h', 'i' })));

			TempFile file;
			AssertOk(writer.SaveToFile(file.GetPath()));

			ArchiveReader reader;
			AssertOk(reader.OpenFile(file.GetPath()));

			PlayerProfile profile;
			GameSettings settings;
			Assert::IsTrue(LoadTypedAsset(reader, kProfileId, profile).IsOk());
			Assert::IsTrue(LoadTypedAsset(reader, kSettingsId, settings).IsOk());
			Assert::IsTrue(profile == MakeProfile());
			Assert::IsTrue(settings == MakeSettings());
		}

		TEST_METHOD(PayloadStartsWithTheTypeTag)
		{
			const std::vector<std::byte> payload = EncodeTypedAsset(MakeSettings());
			AssertBytesEqual(Bytes({ 'G', 'S', 'E', 'T' }), std::span(payload).first(4));
		}

		TEST_METHOD(LoadingAsAnotherTypeIsRejected)
		{
			ArchiveWriter writer;
			AssertOk(AddTypedAsset(writer, kProfileId, MakeProfile()));
			AssertOk(writer.AddAsset(kSettingsId, AssetType::Text, Bytes({ 'x' })));

			ArchiveReader reader;
			AssertOk(reader.Open(writer.Build()));

			GameSettings settings = MakeSettings();
			AssertLoadFails(AssetLoadError::TagMismatch, LoadTypedAsset(reader, kProfileId, settings));
			AssertLoadFails(AssetLoadError::CategoryMismatch, LoadTypedAsset(reader, kSettingsId, settings));
			Assert::IsTrue(settings == MakeSettings(), L"Output untouched on failure");
		}

		TEST_METHOD(MissingAssetReportsTheArchiveError)
		{
			ArchiveWriter writer;
			ArchiveReader reader;
			AssertOk(reader.Open(writer.Build()));

			PlayerProfile profile;
			const AssetLoadResult result = LoadTypedAsset(reader, kProfileId, profile);
			AssertLoadFails(AssetLoadError::ArchiveFailure, result);
			Assert::AreEqual(ToString(ArchiveError::AssetNotFound), result.GetMessage());
		}

		TEST_METHOD(TrailingBytesAreRejected)
		{
			std::vector<std::byte> payload = EncodeTypedAsset(MakeSettings());
			payload.push_back(std::byte{ 0 });

			GameSettings settings;
			AssertLoadFails(AssetLoadError::TrailingBytes, DecodeTypedAsset(payload, settings));
		}

		TEST_METHOD(EveryTruncationOfAPayloadIsRejected)
		{
			const std::vector<std::byte> payload = EncodeTypedAsset(MakeProfile());
			for (std::size_t length = 0; length < payload.size(); ++length)
			{
				PlayerProfile profile = MakeProfile();
				profile.level = 99;
				Assert::IsFalse(DecodeTypedAsset(std::span(payload).first(length), profile).IsOk());
				Assert::AreEqual(99u, profile.level);
			}
		}
	};
}
