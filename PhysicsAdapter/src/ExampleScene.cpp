#include "PhysicsIO/ExampleScene.h"

#include <cmath>

namespace physics_io
{
    namespace
    {
        using physics::Collider;
        using physics::DistanceJoint;
        using physics::MotionType;
        using physics::RigidBody;
        using physics::ShapeType;
        using physics::Vec3;

        // Principal moments of inertia of solid shapes (kg.m^2).
        Vec3 SphereInertia(float mass, float radius) noexcept
        {
            const float i = 0.4f * mass * radius * radius;
            return { i, i, i };
        }

        Vec3 BoxInertia(float mass, Vec3 half) noexcept
        {
            const float k = mass / 3.0f; // m/12 * (2h)^2 = m/3 * h^2
            return { k * (half.y * half.y + half.z * half.z), k * (half.x * half.x + half.z * half.z), k * (half.x * half.x + half.y * half.y) };
        }

        RigidBody Body(std::uint32_t id, MotionType motion, Vec3 position, float mass, Vec3 inertia)
        {
            RigidBody body;
            body.id = id;
            body.motion = motion;
            body.position = position;
            body.mass = mass;
            body.localInertia = inertia;
            return body;
        }

        Collider Sphere(std::uint32_t id, float radius)
        {
            Collider collider;
            collider.id = id;
            collider.shape = ShapeType::Sphere;
            collider.radius = radius;
            return collider;
        }

        Collider Box(std::uint32_t id, Vec3 half)
        {
            Collider collider;
            collider.id = id;
            collider.shape = ShapeType::Box;
            collider.halfExtents = half;
            return collider;
        }
    }

    void BuildExampleWorld(physics::World& world)
    {
        world.Clear();
        world.gravity = { 0.0f, -9.81f, 0.0f };
        world.fixedTimestep = 1.0f / 60.0f;

        // 1: static ground, 20 x 1 x 20 m.
        RigidBody* ground = world.CreateBody(Body(1, MotionType::Static, { 0.0f, -0.5f, 0.0f }, 0.0f, {}));
        Collider groundShape = Box(101, { 10.0f, 0.5f, 10.0f });
        groundShape.friction = 0.8f;
        world.AddCollider(*ground, groundShape);

        // 2: bouncy ball.
        RigidBody ballDesc = Body(2, MotionType::Dynamic, { -2.0f, 5.0f, 0.0f }, 2.0f, SphereInertia(2.0f, 0.5f));
        ballDesc.linearVelocity = { 1.0f, 0.0f, 0.0f };
        RigidBody* ball = world.CreateBody(ballDesc);
        Collider ballShape = Sphere(102, 0.5f);
        ballShape.restitution = 0.7f;
        world.AddCollider(*ball, ballShape);

        // 3: tumbling crate, rotated 30 degrees around Z, spinning.
        RigidBody crateDesc = Body(3, MotionType::Dynamic, { 0.0f, 3.0f, 0.0f }, 10.0f, BoxInertia(10.0f, { 0.5f, 0.5f, 0.5f }));
        crateDesc.orientation = physics::FromAxisAngle({ 0.0f, 0.0f, 1.0f }, 0.5235988f);
        crateDesc.angularVelocity = { 0.0f, 1.5f, 0.5f };
        crateDesc.linearDamping = 0.05f;
        crateDesc.angularDamping = 0.1f;
        RigidBody* crate = world.CreateBody(crateDesc);
        world.AddCollider(*crate, Box(103, { 0.5f, 0.5f, 0.5f }));

        // 4: capsule (character-like), standing.
        RigidBody* capsule = world.CreateBody(Body(4, MotionType::Dynamic, { 2.0f, 2.0f, 1.0f }, 70.0f, { 6.0f, 1.5f, 6.0f }));
        Collider capsuleShape;
        capsuleShape.id = 104;
        capsuleShape.shape = ShapeType::Capsule;
        capsuleShape.radius = 0.35f;
        capsuleShape.halfHeight = 0.6f;
        world.AddCollider(*capsule, capsuleShape);

        // 5: kinematic platform sliding along X.
        RigidBody platformDesc = Body(5, MotionType::Kinematic, { -4.0f, 1.0f, -3.0f }, 0.0f, {});
        platformDesc.linearVelocity = { 0.5f, 0.0f, 0.0f };
        RigidBody* platform = world.CreateBody(platformDesc);
        world.AddCollider(*platform, Box(105, { 1.5f, 0.1f, 1.5f }));

        // 6 + 7: pendulum. Static anchor, dynamic bob hanging 2 m below with a sideways push.
        RigidBody* anchor = world.CreateBody(Body(6, MotionType::Static, { 4.0f, 6.0f, 0.0f }, 0.0f, {}));
        world.AddCollider(*anchor, Sphere(106, 0.1f));

        RigidBody bobDesc = Body(7, MotionType::Dynamic, { 4.0f, 4.0f, 0.0f }, 1.0f, SphereInertia(1.0f, 0.25f));
        bobDesc.linearVelocity = { 0.0f, 0.0f, 2.0f };
        RigidBody* bob = world.CreateBody(bobDesc);
        world.AddCollider(*bob, Sphere(107, 0.25f));

        DistanceJoint rope;
        rope.id = 201;
        rope.bodyA = anchor;
        rope.bodyB = bob;
        rope.restLength = 2.0f;
        world.AddDistanceJoint(rope);
    }

    void BuildGridWorld(physics::World& world, std::uint32_t bodyCount)
    {
        world.Clear();

        RigidBody* ground = world.CreateBody(Body(1, MotionType::Static, { 0.0f, -0.5f, 0.0f }, 0.0f, {}));
        world.AddCollider(*ground, Box(1'000'000, { 1000.0f, 0.5f, 1000.0f }));

        const auto side = static_cast<std::uint32_t>(std::ceil(std::sqrt(static_cast<double>(bodyCount))));
        RigidBody* previous = nullptr;
        std::uint32_t jointId = 2'000'000;

        for (std::uint32_t i = 0; i < bodyCount; ++i)
        {
            const std::uint32_t row = i / side;
            const std::uint32_t column = i % side;
            const Vec3 half{ 0.4f, 0.4f, 0.4f };

            RigidBody description = Body(i + 2, MotionType::Dynamic,
                { static_cast<float>(column), 2.0f + static_cast<float>(i % 7), static_cast<float>(row) },
                5.0f, BoxInertia(5.0f, half));
            description.angularVelocity = { 0.0f, 0.1f * static_cast<float>(i % 5), 0.0f };

            RigidBody* body = world.CreateBody(description);
            world.AddCollider(*body, Box(i + 2, half));

            if (previous != nullptr && column != 0)
            {
                DistanceJoint joint;
                joint.id = jointId++;
                joint.bodyA = previous;
                joint.bodyB = body;
                joint.restLength = 1.0f;
                world.AddDistanceJoint(joint);
            }
            previous = body;
        }
    }
}
