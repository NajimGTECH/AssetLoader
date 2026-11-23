#include "PhysicsIO/SceneAdapter.h"

#include <cmath>
#include <unordered_set>
#include <utility>

#include "PhysicsIO/SceneSerialization.h"

namespace physics_io
{
    namespace
    {
        // --- Conversions between persistent records and engine types (no shared layout) ---------

        [[nodiscard]] physics::Vec3 ToEngine(Float3 v) noexcept { return { v.x, v.y, v.z }; }
        [[nodiscard]] physics::Quat ToEngine(Quaternion q) noexcept { return { q.w, q.x, q.y, q.z }; }
        [[nodiscard]] Float3 ToRecord(physics::Vec3 v) noexcept { return { v.x, v.y, v.z }; }
        [[nodiscard]] Quaternion ToRecord(physics::Quat q) noexcept { return { q.w, q.x, q.y, q.z }; }

        [[nodiscard]] physics::MotionType ToEngine(MotionKind kind) noexcept
        {
            switch (kind)
            {
            case MotionKind::Static:    return physics::MotionType::Static;
            case MotionKind::Kinematic: return physics::MotionType::Kinematic;
            case MotionKind::Dynamic:   break;
            }
            return physics::MotionType::Dynamic;
        }

        [[nodiscard]] MotionKind ToRecord(physics::MotionType type) noexcept
        {
            switch (type)
            {
            case physics::MotionType::Static:    return MotionKind::Static;
            case physics::MotionType::Kinematic: return MotionKind::Kinematic;
            case physics::MotionType::Dynamic:   break;
            }
            return MotionKind::Dynamic;
        }

        [[nodiscard]] physics::ShapeType ToEngine(ShapeKind kind) noexcept
        {
            switch (kind)
            {
            case ShapeKind::Box:     return physics::ShapeType::Box;
            case ShapeKind::Capsule: return physics::ShapeType::Capsule;
            case ShapeKind::Sphere:  break;
            }
            return physics::ShapeType::Sphere;
        }

        [[nodiscard]] ShapeKind ToRecord(physics::ShapeType type) noexcept
        {
            switch (type)
            {
            case physics::ShapeType::Box:     return ShapeKind::Box;
            case physics::ShapeType::Capsule: return ShapeKind::Capsule;
            case physics::ShapeType::Sphere:  break;
            }
            return ShapeKind::Sphere;
        }

        // --- Validation helpers ------------------------------------------------------------------

        [[nodiscard]] bool IsFinite(float value) noexcept { return std::isfinite(value); }
        [[nodiscard]] bool IsFinite(Float3 v) noexcept { return IsFinite(v.x) && IsFinite(v.y) && IsFinite(v.z); }
        [[nodiscard]] bool IsPositive(float value) noexcept { return IsFinite(value) && value > 0.0f; }
        [[nodiscard]] bool IsNonNegative(float value) noexcept { return IsFinite(value) && value >= 0.0f; }
        [[nodiscard]] bool IsNonNegative(Float3 v) noexcept { return IsNonNegative(v.x) && IsNonNegative(v.y) && IsNonNegative(v.z); }
        [[nodiscard]] bool IsPositive(Float3 v) noexcept { return IsPositive(v.x) && IsPositive(v.y) && IsPositive(v.z); }

        // Saved orientations come from an engine that normalizes them every step: a small drift is
        // accepted (and kept bit-exact), anything further means the data is wrong.
        [[nodiscard]] bool IsUnitQuaternion(Quaternion q) noexcept
        {
            if (!IsFinite(q.w) || !IsFinite(q.x) || !IsFinite(q.y) || !IsFinite(q.z))
            {
                return false;
            }
            const float squaredLength = q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z;
            return std::abs(squaredLength - 1.0f) <= 1e-3f;
        }

        [[nodiscard]] SceneResult ValidateCollider(const ColliderRecord& collider)
        {
            const bool sizeIsValid =
                (collider.shape == ShapeKind::Sphere && IsPositive(collider.radius))
                || (collider.shape == ShapeKind::Box && IsPositive(collider.halfExtents))
                || (collider.shape == ShapeKind::Capsule && IsPositive(collider.radius) && IsNonNegative(collider.halfHeight));

            if (!sizeIsValid || !IsNonNegative(collider.friction)
                || !IsNonNegative(collider.restitution) || collider.restitution > 1.0f
                || !IsFinite(collider.localPosition))
            {
                return { SceneError::InvalidShape, collider.id };
            }
            if (!IsUnitQuaternion(collider.localRotation))
            {
                return { SceneError::InvalidOrientation, collider.id };
            }
            return {};
        }

        [[nodiscard]] SceneResult ValidateBody(const BodyRecord& body)
        {
            if (!IsFinite(body.position) || !IsFinite(body.linearVelocity) || !IsFinite(body.angularVelocity)
                || !IsNonNegative(body.linearDamping) || !IsNonNegative(body.angularDamping))
            {
                return { SceneError::InvalidBodyState, body.id };
            }
            if (!IsUnitQuaternion(body.orientation))
            {
                return { SceneError::InvalidOrientation, body.id };
            }

            const bool massIsValid = body.motion == MotionKind::Dynamic
                ? IsPositive(body.mass) && IsPositive(body.inertia)
                : IsNonNegative(body.mass) && IsNonNegative(body.inertia);
            if (!massIsValid)
            {
                return { SceneError::InvalidMass, body.id };
            }
            return {};
        }

        /** @brief Builds a complete world from a validated record. Throws only std::bad_alloc. */
        [[nodiscard]] SceneResult BuildWorld(const SceneRecord& scene, physics::World& world)
        {
            world.gravity = ToEngine(scene.gravity);
            world.fixedTimestep = scene.fixedTimestep;
            world.SetClock(scene.stepIndex, scene.time);

            for (const BodyRecord& record : scene.bodies)
            {
                physics::RigidBody description;
                description.id = record.id;
                description.motion = ToEngine(record.motion);
                description.position = ToEngine(record.position);
                description.orientation = ToEngine(record.orientation);
                description.linearVelocity = ToEngine(record.linearVelocity);
                description.angularVelocity = ToEngine(record.angularVelocity);
                description.mass = record.mass;
                description.localInertia = ToEngine(record.inertia);
                description.linearDamping = record.linearDamping;
                description.angularDamping = record.angularDamping;

                physics::RigidBody* body = world.CreateBody(description); // derived data computed here
                if (body == nullptr)
                {
                    return { SceneError::DuplicateBodyId, record.id };
                }

                for (const ColliderRecord& colliderRecord : record.colliders)
                {
                    physics::Collider collider;
                    collider.id = colliderRecord.id;
                    collider.shape = ToEngine(colliderRecord.shape);
                    collider.radius = colliderRecord.radius;
                    collider.halfExtents = ToEngine(colliderRecord.halfExtents);
                    collider.halfHeight = colliderRecord.halfHeight;
                    collider.localPosition = ToEngine(colliderRecord.localPosition);
                    collider.localRotation = ToEngine(colliderRecord.localRotation);
                    collider.friction = colliderRecord.friction;
                    collider.restitution = colliderRecord.restitution;

                    // Rebuilds the collider -> body pointer, the body's collider list and the bounds.
                    if (world.AddCollider(*body, collider) == nullptr)
                    {
                        return { SceneError::DuplicateColliderId, colliderRecord.id };
                    }
                }
            }

            for (const JointRecord& record : scene.joints)
            {
                // Stable IDs -> pointers.
                physics::DistanceJoint joint;
                joint.id = record.id;
                joint.bodyA = world.FindBody(record.bodyA);
                joint.bodyB = world.FindBody(record.bodyB);
                joint.localAnchorA = ToEngine(record.anchorA);
                joint.localAnchorB = ToEngine(record.anchorB);
                joint.restLength = record.restLength;

                if (joint.bodyA == nullptr || joint.bodyB == nullptr)
                {
                    return { SceneError::InvalidReference, record.id };
                }
                if (world.AddDistanceJoint(joint) == nullptr)
                {
                    return { SceneError::DuplicateJointId, record.id };
                }
            }

            return {};
        }
    }

    std::string_view ToString(SceneError error) noexcept
    {
        switch (error)
        {
        case SceneError::None:                return "no error";
        case SceneError::InvalidSettings:     return "invalid scene settings (gravity not finite or timestep outside (0, 1] s)";
        case SceneError::InvalidClock:        return "invalid simulation time";
        case SceneError::InvalidBodyId:       return "invalid body ID (0 is reserved)";
        case SceneError::DuplicateBodyId:     return "two bodies have the same ID";
        case SceneError::InvalidBodyState:    return "body position, velocity or damping is not valid";
        case SceneError::InvalidOrientation:  return "orientation is not a unit quaternion";
        case SceneError::InvalidMass:         return "invalid mass or inertia";
        case SceneError::InvalidColliderId:   return "invalid collider ID (0 is reserved)";
        case SceneError::DuplicateColliderId: return "two colliders have the same ID";
        case SceneError::InvalidShape:        return "invalid collider shape parameters or material";
        case SceneError::InvalidJointId:      return "invalid joint ID (0 is reserved)";
        case SceneError::DuplicateJointId:    return "two joints have the same ID";
        case SceneError::InvalidReference:    return "a joint references a body that is not in the scene";
        case SceneError::SelfReference:       return "a joint connects a body to itself";
        case SceneError::InvalidJoint:        return "invalid joint anchor or rest length";
        case SceneError::WorldNotEmpty:       return "the target world is not empty";
        }
        return "unknown scene error";
    }

    std::string SceneResult::GetMessage() const
    {
        std::string message(ToString(m_error));
        if (m_objectId != 0)
        {
            message += " (ID " + std::to_string(m_objectId) + ")";
        }
        return message;
    }

    SceneRecord CaptureScene(const physics::World& world, std::string name)
    {
        SceneRecord scene;
        scene.name = std::move(name);
        scene.gravity = ToRecord(world.gravity);
        scene.fixedTimestep = world.fixedTimestep;
        scene.stepIndex = world.GetStepIndex();
        scene.time = world.GetTime();

        scene.bodies.reserve(world.GetBodies().size());
        for (const auto& body : world.GetBodies())
        {
            BodyRecord record;
            record.id = body->id;
            record.motion = ToRecord(body->motion);
            record.position = ToRecord(body->position);
            record.orientation = ToRecord(body->orientation);
            record.linearVelocity = ToRecord(body->linearVelocity);
            record.angularVelocity = ToRecord(body->angularVelocity);
            record.mass = body->mass;
            record.inertia = ToRecord(body->localInertia);
            record.linearDamping = body->linearDamping;
            record.angularDamping = body->angularDamping;

            record.colliders.reserve(body->colliders.size());
            for (const physics::Collider* collider : body->colliders)
            {
                record.colliders.push_back({
                    collider->id, ToRecord(collider->shape), collider->radius, ToRecord(collider->halfExtents),
                    collider->halfHeight, ToRecord(collider->localPosition), ToRecord(collider->localRotation),
                    collider->friction, collider->restitution });
            }

            scene.bodies.push_back(std::move(record));
        }

        scene.joints.reserve(world.GetJoints().size());
        for (const auto& joint : world.GetJoints())
        {
            scene.joints.push_back({
                joint->id, joint->bodyA->id, joint->bodyB->id,
                ToRecord(joint->localAnchorA), ToRecord(joint->localAnchorB), joint->restLength });
        }

        return scene;
    }

    SceneResult ValidateScene(const SceneRecord& scene)
    {
        if (!IsFinite(scene.gravity) || !IsPositive(scene.fixedTimestep) || scene.fixedTimestep > 1.0f)
        {
            return SceneError::InvalidSettings;
        }
        if (!std::isfinite(scene.time) || scene.time < 0.0)
        {
            return SceneError::InvalidClock;
        }

        std::unordered_set<std::uint32_t> bodyIds;
        std::unordered_set<std::uint32_t> colliderIds;
        bodyIds.reserve(scene.bodies.size());

        for (const BodyRecord& body : scene.bodies)
        {
            if (body.id == 0)
            {
                return SceneError::InvalidBodyId;
            }
            if (!bodyIds.insert(body.id).second)
            {
                return { SceneError::DuplicateBodyId, body.id };
            }
            if (const SceneResult result = ValidateBody(body); !result)
            {
                return result;
            }

            for (const ColliderRecord& collider : body.colliders)
            {
                if (collider.id == 0)
                {
                    return { SceneError::InvalidColliderId, body.id };
                }
                if (!colliderIds.insert(collider.id).second)
                {
                    return { SceneError::DuplicateColliderId, collider.id };
                }
                if (const SceneResult result = ValidateCollider(collider); !result)
                {
                    return result;
                }
            }
        }

        std::unordered_set<std::uint32_t> jointIds;
        for (const JointRecord& joint : scene.joints)
        {
            if (joint.id == 0)
            {
                return SceneError::InvalidJointId;
            }
            if (!jointIds.insert(joint.id).second)
            {
                return { SceneError::DuplicateJointId, joint.id };
            }
            if (!bodyIds.contains(joint.bodyA) || !bodyIds.contains(joint.bodyB))
            {
                return { SceneError::InvalidReference, joint.id };
            }
            if (joint.bodyA == joint.bodyB)
            {
                return { SceneError::SelfReference, joint.id };
            }
            if (!IsFinite(joint.anchorA) || !IsFinite(joint.anchorB) || !IsNonNegative(joint.restLength))
            {
                return { SceneError::InvalidJoint, joint.id };
            }
        }

        return {};
    }

    SceneResult ReplaceWorld(const SceneRecord& scene, physics::World& world)
    {
        if (const SceneResult result = ValidateScene(scene); !result)
        {
            return result;
        }

        // Built on the side, moved in only when complete: no partially loaded state.
        physics::World rebuilt;
        if (const SceneResult result = BuildWorld(scene, rebuilt); !result)
        {
            return result;
        }

        world = std::move(rebuilt);
        return {};
    }

    SceneResult LoadIntoEmptyWorld(const SceneRecord& scene, physics::World& world)
    {
        if (!world.IsEmpty())
        {
            return SceneError::WorldNotEmpty;
        }
        return ReplaceWorld(scene, world);
    }

    asset::ArchiveResult SaveWorld(asset::ArchiveWriter& writer, asset::AssetId id, const physics::World& world, std::string name)
    {
        return asset::AddTypedAsset(writer, id, CaptureScene(world, std::move(name)));
    }

    std::string WorldLoadResult::GetMessage() const
    {
        if (!assetResult.IsOk())
        {
            return std::string(assetResult.GetMessage());
        }
        return sceneResult.GetMessage();
    }

    WorldLoadResult LoadWorld(const asset::ArchiveReader& reader, asset::AssetId id, physics::World& world)
    {
        WorldLoadResult result;

        SceneRecord scene;
        result.assetResult = asset::LoadTypedAsset(reader, id, scene);
        if (result.assetResult.IsOk())
        {
            result.sceneResult = ReplaceWorld(scene, world);
        }
        return result;
    }
}
