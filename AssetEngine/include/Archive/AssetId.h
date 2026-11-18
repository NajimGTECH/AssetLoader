#pragma once
#include <cstdint>
#include <string_view>

/**
 * @file AssetId.h
 * @brief Identifiers and type tags of the assets stored in an archive.
 *
 *  - An AssetId is a 64-bit number, unique inside an archive. It can be chosen freely, or derived
 *    from a name with MakeAssetId() (64-bit FNV-1a hash), e.g. MakeAssetId("textures/player.png").
 *  - The value 0 (kInvalidAssetId) is reserved and never stored in an archive.
 *  - AssetType tells the loader how to interpret the bytes of an asset. The archive itself never
 *    looks inside the data: the type is only metadata stored in the directory.
 */
namespace asset
{
	/** @brief Unique identifier of an asset inside an archive. */
	using AssetId = std::uint64_t;

	/** @brief Reserved value, never a valid asset ID. */
	inline constexpr AssetId kInvalidAssetId = 0;

	/**
	 * @brief Builds an AssetId from a name with the 64-bit FNV-1a hash.
	 *
	 * The hash is stable across compilers and platforms (it only depends on the bytes of the name),
	 * so the same name always gives the same ID. `constexpr`, so it can be used for compile-time
	 * constants:
	 *
	 * @code
	 * constexpr asset::AssetId kPlayerTexture = asset::MakeAssetId("textures/player.png");
	 * @endcode
	 *
	 * Two different names can in theory give the same ID. The ArchiveWriter reports it
	 * (ArchiveError::DuplicateAssetId) instead of silently overwriting an asset.
	 */
	[[nodiscard]] constexpr AssetId MakeAssetId(std::string_view name) noexcept
	{
		constexpr std::uint64_t kOffsetBasis = 0xCBF29CE484222325ull;
		constexpr std::uint64_t kPrime = 0x00000100000001B3ull;

		std::uint64_t hash = kOffsetBasis;
		for (const char character : name)
		{
			hash ^= static_cast<std::uint8_t>(character);
			hash *= kPrime;
		}
		return hash;
	}

	/**
	 * @brief Kind of data stored in an asset. Stored as a uint32 in the archive directory.
	 *
	 * The numeric values are part of the file format: never renumber an existing entry, only add
	 * new ones before `Count`.
	 */
	enum class AssetType : std::uint32_t
	{
		/** @brief Not a valid type. Never stored in an archive. */
		Unknown = 0,

		/** @brief Opaque bytes, interpreted by the application. */
		Binary = 1,
		/** @brief Text (UTF-8). */
		Text = 2,
		/** @brief Image / texture data. */
		Texture = 3,
		/** @brief Geometry (vertices, indices...). */
		Mesh = 4,
		/** @brief Sound data. */
		Audio = 5,
		/** @brief Material description. */
		Material = 6,
		/** @brief Shader source or bytecode. */
		Shader = 7,
		/** @brief Scene / level description. */
		Scene = 8,

		/** @brief Number of values, not a valid type. Keep it last. */
		Count,
	};

	/** @brief True if `type` is one of the valid types (not Unknown, not Count, not an unknown number). */
	[[nodiscard]] constexpr bool IsValidAssetType(AssetType type) noexcept
	{
		const auto value = static_cast<std::uint32_t>(type);
		return value > static_cast<std::uint32_t>(AssetType::Unknown)
			&& value < static_cast<std::uint32_t>(AssetType::Count);
	}

	/** @brief Short name of an asset type, for diagnostics ("Texture", "Mesh"...). */
	[[nodiscard]] constexpr std::string_view ToString(AssetType type) noexcept
	{
		switch (type)
		{
		case AssetType::Unknown:  return "Unknown";
		case AssetType::Binary:   return "Binary";
		case AssetType::Text:     return "Text";
		case AssetType::Texture:  return "Texture";
		case AssetType::Mesh:     return "Mesh";
		case AssetType::Audio:    return "Audio";
		case AssetType::Material: return "Material";
		case AssetType::Shader:   return "Shader";
		case AssetType::Scene:    return "Scene";
		case AssetType::Count:    break;
		}
		return "Invalid";
	}
}
