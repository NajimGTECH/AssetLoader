#pragma once

#include <cstdint>
#include <memory>
#include <span>
#include <unordered_map>
#include <vector>

#include "Physics/MathTypes.h"

/*
* Rigid-body world of the physics engine (stand-in for the year-long engine project).
*
* This is the engine's *in-memory* representation. It is deliberately not designed for storage:
* bodies are heap objects that reference each other through raw pointers (collider -> body,
* joint -> bodies), and every body caches derived data (inverse mass, world-space inverse inertia,
* bounding boxes) that is recomputed rather than saved. The persistent format lives in the
* PhysicsAdapter project and refers to objects through their stable IDs instead.
*
* The engine knows nothing about archives or serialization.
*/
namespace physics
{
    enum class MotionType : std::uint8_t
    {
        Static,    // never moves, infinite mass
        Dynamic,   // moved by gravity, forces and joints
        Kinematic, // moved by its velocity only, infinite mass
    };

    enum class ShapeType : std::uint8_t
    {
        Sphere,  // radius
        Box,     // halfExtents
        Capsule, // radius + halfHeight (segment along the local Y axis)
    };

    struct RigidBody;

    struct Collider
    {
        std::uint32_t id = 0;
        ShapeType shape = ShapeType::Sphere;
        float radius = 0.5f;
        Vec3 halfExtents{ 0.5f, 0.5f, 0.5f };
        float halfHeight = 0.5f;
        Vec3 localPosition{};   // relative to the body's centre of mass, in body space
        Quat localRotation{};
        float friction = 0.5f;
        float restitution = 0.0f;

        // --- Derived data (rebuilt, never saved) ---
        RigidBody* body = nullptr;
        Aabb worldBounds{};
    };

    struct RigidBody
    {
        std::uint32_t id = 0;
        MotionType motion = MotionType::Dynamic;
        Vec3 position{};        // centre of mass, world space
        Quat orientation{};     // body space -> world space
        Vec3 linearVelocity{};  // world space, m/s
        Vec3 angularVelocity{}; // world space, rad/s
        float mass = 1.0f;      // kg (ignored for static and kinematic bodies)
        Vec3 localInertia{ 1.0f, 1.0f, 1.0f }; // principal moments, body space, kg.m^2
        float linearDamping = 0.0f;
        float angularDamping = 0.0f;

        // --- Derived data (rebuilt, never saved) ---
        float inverseMass = 1.0f;
        Vec3 inverseLocalInertia{ 1.0f, 1.0f, 1.0f };
        Mat3 inverseWorldInertia{};
        Vec3 force{};           // accumulated during a step, cleared at its end
        Vec3 torque{};
        std::vector<Collider*> colliders;
        Aabb bounds{};

        /** @brief Recomputes inverse mass and inertia, world inertia and bounding boxes. */
        void UpdateDerivedData() noexcept;
    };

    /** @brief Keeps the distance between two anchor points (body space) equal to restLength. */
    struct DistanceJoint
    {
        std::uint32_t id = 0;
        RigidBody* bodyA = nullptr;
        RigidBody* bodyB = nullptr;
        Vec3 localAnchorA{};
        Vec3 localAnchorB{};
        float restLength = 1.0f;
    };

    class World
    {
    public:
        World() = default;
        World(const World&) = delete;
        World& operator=(const World&) = delete;
        World(World&&) noexcept = default;
        World& operator=(World&&) noexcept = default;

        // --- Settings ---
        Vec3 gravity{ 0.0f, -9.81f, 0.0f };
        float fixedTimestep = 1.0f / 60.0f;

        // --- Simulation clock ---
        [[nodiscard]] std::uint64_t GetStepIndex() const noexcept { return m_stepIndex; }
        [[nodiscard]] double GetTime() const noexcept { return m_time; }
        void SetClock(std::uint64_t stepIndex, double time) noexcept { m_stepIndex = stepIndex; m_time = time; }

        /**
         * @brief Creates a body. Returns nullptr if the ID is 0 or already used.
         * The returned pointer stays valid until the body's world is cleared or destroyed.
         */
        RigidBody* CreateBody(const RigidBody& description);

        /** @brief Attaches a collider to a body of this world. Returns nullptr if the ID is 0 or used. */
        Collider* AddCollider(RigidBody& body, const Collider& description);

        /** @brief Adds a distance joint between two different bodies of this world. Returns nullptr if invalid. */
        DistanceJoint* AddDistanceJoint(const DistanceJoint& description);

        [[nodiscard]] RigidBody* FindBody(std::uint32_t id) noexcept;
        [[nodiscard]] const RigidBody* FindBody(std::uint32_t id) const noexcept;

        [[nodiscard]] std::span<const std::unique_ptr<RigidBody>> GetBodies() const noexcept { return m_bodies; }
        [[nodiscard]] std::span<const std::unique_ptr<DistanceJoint>> GetJoints() const noexcept { return m_joints; }
        [[nodiscard]] std::size_t GetColliderCount() const noexcept { return m_colliders.size(); }
        [[nodiscard]] bool IsEmpty() const noexcept { return m_bodies.empty() && m_joints.empty(); }

        /**
         * @brief Advances the simulation by one fixed timestep: gravity and forces, velocity
         * integration, damping, position integration, joint projection, simplified contacts against
         * static bodies (bounding boxes, resolved upwards), derived data. At the end of the step,
         * force accumulators are zero and every derived value is up to date.
         */
        void Step() noexcept;

        /** @brief Removes every body, collider and joint, and resets the clock. Settings are kept. */
        void Clear() noexcept;

    private:
        std::vector<std::unique_ptr<RigidBody>> m_bodies;
        std::vector<std::unique_ptr<Collider>> m_colliders;
        std::vector<std::unique_ptr<DistanceJoint>> m_joints;
        std::unordered_map<std::uint32_t, RigidBody*> m_bodyById;
        std::unordered_map<std::uint32_t, Collider*> m_colliderById;
        std::unordered_map<std::uint32_t, DistanceJoint*> m_jointById;
        std::uint64_t m_stepIndex = 0;
        double m_time = 0.0;
    };
}
