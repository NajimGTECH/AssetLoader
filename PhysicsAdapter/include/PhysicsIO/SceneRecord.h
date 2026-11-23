#pragma once

#include <cstdint>
#include <string>
#include <vector>

/*
* Persistent representation of a physics scene (what is stored in the archive).
*
* These records are plain values, independent of the engine's in-memory types: no pointers, no
* cached data, no engine headers. Objects refer to each other only through stable IDs
* (a joint stores bodyA / bodyB IDs, not pointers), and the adapter rebuilds the pointers on load.
* The same record describes an authored scene (stepIndex = 0) and a snapshot captured at the end
* of a simulation step.
*
* Conventions
*   - Right-handed coordinates, Y up. SI units: metres, kilograms, seconds, radians.
*   - position: centre of mass, world space. orientation: unit quaternion (w, x, y, z), body -> world.
*   - linearVelocity (m/s) and angularVelocity (rad/s) are expressed in world space.
*   - mass in kg. inertia: principal moments of inertia (kg.m^2) along the body's local axes.
*     Static and kinematic bodies have infinite mass: their mass and inertia are stored but ignored.
*   - Colliders: localPosition / localRotation relative to the body. Sphere uses radius, Box uses
*     halfExtents, Capsule uses radius + halfHeight (segment along the collider's local Y axis).
*   - Joint anchors are in the body space of bodyA / bodyB.
*   - IDs are non-zero and unique per kind (bodies, colliders, joints) within one scene.
*   - Bodies, colliders and joints are stored in simulation order: restoring keeps that order, so
*     a restored world steps exactly like the original one.
*/
namespace physics_io
{
    struct Float3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        [[nodiscard]] friend bool operator==(const Float3&, const Float3&) = default;
    };

    struct Quaternion
    {
        float w = 1.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        [[nodiscard]] friend bool operator==(const Quaternion&, const Quaternion&) = default;
    };

    enum class MotionKind : std::uint8_t
    {
        Static = 0,
        Dynamic = 1,
        Kinematic = 2,
    };

    enum class ShapeKind : std::uint8_t
    {
        Sphere = 0,
        Box = 1,
        Capsule = 2,
    };

    struct ColliderRecord
    {
        std::uint32_t id = 0;
        ShapeKind shape = ShapeKind::Sphere;
        float radius = 0.5f;
        Float3 halfExtents{ 0.5f, 0.5f, 0.5f };
        float halfHeight = 0.5f;
        Float3 localPosition{};
        Quaternion localRotation{};
        float friction = 0.5f;
        float restitution = 0.0f;

        [[nodiscard]] friend bool operator==(const ColliderRecord&, const ColliderRecord&) = default;
    };

    struct BodyRecord
    {
        // --- Scene version 1 ---
        std::uint32_t id = 0;
        MotionKind motion = MotionKind::Dynamic;
        Float3 position{};
        Quaternion orientation{};
        Float3 linearVelocity{};
        Float3 angularVelocity{};
        float mass = 1.0f;
        Float3 inertia{ 1.0f, 1.0f, 1.0f };

        // --- Added in scene version 2 (default 0 = no damping when reading version 1) ---
        float linearDamping = 0.0f;
        float angularDamping = 0.0f;

        // --- Scene version 1 ---
        std::vector<ColliderRecord> colliders;

        [[nodiscard]] friend bool operator==(const BodyRecord&, const BodyRecord&) = default;
    };

    struct JointRecord
    {
        std::uint32_t id = 0;
        std::uint32_t bodyA = 0; // stable body ID, resolved to a pointer on load
        std::uint32_t bodyB = 0;
        Float3 anchorA{};
        Float3 anchorB{};
        float restLength = 1.0f;

        [[nodiscard]] friend bool operator==(const JointRecord&, const JointRecord&) = default;
    };

    struct SceneRecord
    {
        std::string name;
        Float3 gravity{ 0.0f, -9.81f, 0.0f };
        float fixedTimestep = 1.0f / 60.0f;
        std::uint64_t stepIndex = 0; // number of steps simulated when the record was captured
        double time = 0.0;           // simulated time, seconds
        std::vector<BodyRecord> bodies;
        std::vector<JointRecord> joints;

        [[nodiscard]] friend bool operator==(const SceneRecord&, const SceneRecord&) = default;
    };
}
