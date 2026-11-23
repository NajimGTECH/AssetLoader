#pragma once

#include <cstdint>
#include <string>
#include <string_view>

#include "Archive/ArchiveReader.h"
#include "Archive/ArchiveWriter.h"
#include "Assets/TypedAsset.h"
#include "Physics/World.h"
#include "PhysicsIO/SceneRecord.h"

/*
* Adapter between the physics engine (physics::World) and its persistent form (SceneRecord).
*
*   Capture:  World -> SceneRecord. Call it between two World::Step() calls, i.e. at the end of a
*             step: force accumulators are then zero and every derived value is consistent, so
*             nothing transient needs to be saved.
*   Restore:  SceneRecord -> World, in two flavours:
*             - LoadIntoEmptyWorld: the target world must have no body and no joint;
*             - ReplaceWorld: the target world's content is replaced.
*             The record is validated first, then a complete new world is built on the side and
*             only moved into the target once everything succeeded. On any error the target world
*             is left exactly as it was: there is never a partially loaded state.
*             Restoring rebuilds what is not stored: collider -> body and joint -> body pointers
*             (from the stable IDs), inverse mass and inertia, world-space inertia and bounding boxes.
*/
namespace physics_io
{
    enum class SceneError : std::uint8_t
    {
        None = 0,
        InvalidSettings,     // gravity not finite, or timestep not in (0, 1] s
        InvalidClock,        // time negative or not finite
        InvalidBodyId,       // body ID 0
        DuplicateBodyId,
        InvalidBodyState,    // non-finite position or velocity, negative damping
        InvalidOrientation,  // quaternion not finite or not of unit length
        InvalidMass,         // dynamic body with mass <= 0, or any non-finite / negative mass or inertia
        InvalidColliderId,   // collider ID 0
        DuplicateColliderId,
        InvalidShape,        // non-positive size, friction < 0, restitution outside [0, 1]
        InvalidJointId,      // joint ID 0
        DuplicateJointId,
        InvalidReference,    // joint referencing a body ID that is not in the scene
        SelfReference,       // joint connecting a body to itself
        InvalidJoint,        // non-finite anchor or negative rest length
        WorldNotEmpty,       // LoadIntoEmptyWorld on a world that has bodies or joints
    };

    [[nodiscard]] std::string_view ToString(SceneError error) noexcept;

    class [[nodiscard]] SceneResult
    {
    public:
        SceneResult() noexcept = default;
        SceneResult(SceneError error, std::uint32_t objectId = 0) noexcept : m_error(error), m_objectId(objectId) {}

        [[nodiscard]] bool IsOk() const noexcept { return m_error == SceneError::None; }
        [[nodiscard]] explicit operator bool() const noexcept { return IsOk(); }
        [[nodiscard]] SceneError GetError() const noexcept { return m_error; }

        /** @brief ID of the offending body, collider or joint (0 when not applicable). */
        [[nodiscard]] std::uint32_t GetObjectId() const noexcept { return m_objectId; }

        /** @brief Diagnostic message, with the offending ID when there is one. */
        [[nodiscard]] std::string GetMessage() const;

    private:
        SceneError m_error = SceneError::None;
        std::uint32_t m_objectId = 0;
    };

    /** @brief Captures the current state of `world` (scene settings, clock, bodies, colliders, joints). */
    [[nodiscard]] SceneRecord CaptureScene(const physics::World& world, std::string name = {});

    /** @brief Checks every rule of the SceneRecord conventions (IDs, references, values). */
    SceneResult ValidateScene(const SceneRecord& scene);

    /** @brief Restores `scene` into `world`, which must be empty. `world` is unchanged on failure. */
    SceneResult LoadIntoEmptyWorld(const SceneRecord& scene, physics::World& world);

    /** @brief Replaces the content of `world` with `scene`. `world` is unchanged on failure. */
    SceneResult ReplaceWorld(const SceneRecord& scene, physics::World& world);

    // --- Archive helpers -------------------------------------------------------------------------

    /** @brief Captures `world` and adds it to the archive under `id` (AssetType::Scene). */
    asset::ArchiveResult SaveWorld(asset::ArchiveWriter& writer, asset::AssetId id, const physics::World& world, std::string name = {});

    /**
     * @brief Loads the scene `id` from an open archive and replaces the content of `world` with it.
     * Decoding errors and scene errors are both reported; `world` is unchanged on any failure.
     */
    struct WorldLoadResult
    {
        asset::AssetLoadResult assetResult; // archive lookup and decoding
        SceneResult sceneResult;            // validation and restoration

        [[nodiscard]] bool IsOk() const noexcept { return assetResult.IsOk() && sceneResult.IsOk(); }
        [[nodiscard]] std::string GetMessage() const;
    };

    [[nodiscard]] WorldLoadResult LoadWorld(const asset::ArchiveReader& reader, asset::AssetId id, physics::World& world);
}
