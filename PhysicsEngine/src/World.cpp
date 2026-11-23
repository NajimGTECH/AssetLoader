#include "Physics/World.h"

#include <algorithm>

namespace physics
{
    namespace
    {
        [[nodiscard]] Vec3 Min(Vec3 a, Vec3 b) noexcept { return { std::min(a.x, b.x), std::min(a.y, b.y), std::min(a.z, b.z) }; }
        [[nodiscard]] Vec3 Max(Vec3 a, Vec3 b) noexcept { return { std::max(a.x, b.x), std::max(a.y, b.y), std::max(a.z, b.z) }; }

        [[nodiscard]] float Inverse(float value) noexcept { return value > 0.0f ? 1.0f / value : 0.0f; }

        /** @brief World-space bounding box of a collider attached to `body`. */
        [[nodiscard]] Aabb ComputeBounds(const RigidBody& body, const Collider& collider) noexcept
        {
            const Quat rotation = body.orientation * collider.localRotation;
            const Vec3 centre = body.position + Rotate(body.orientation, collider.localPosition);

            Vec3 extent{};
            switch (collider.shape)
            {
            case ShapeType::Sphere:
                extent = { collider.radius, collider.radius, collider.radius };
                break;
            case ShapeType::Box:
            {
                // |R| * halfExtents: the extent of an oriented box along each world axis.
                const Mat3 r = RotationMatrix(rotation);
                const Vec3 h = collider.halfExtents;
                extent = {
                    std::abs(r(0, 0)) * h.x + std::abs(r(0, 1)) * h.y + std::abs(r(0, 2)) * h.z,
                    std::abs(r(1, 0)) * h.x + std::abs(r(1, 1)) * h.y + std::abs(r(1, 2)) * h.z,
                    std::abs(r(2, 0)) * h.x + std::abs(r(2, 1)) * h.y + std::abs(r(2, 2)) * h.z,
                };
                break;
            }
            case ShapeType::Capsule:
            {
                const Vec3 axis = Abs(Rotate(rotation, Vec3{ 0.0f, collider.halfHeight, 0.0f }));
                extent = axis + Vec3{ collider.radius, collider.radius, collider.radius };
                break;
            }
            }

            return { centre - extent, centre + extent };
        }
    }

    void RigidBody::UpdateDerivedData() noexcept
    {
        const bool isDynamic = motion == MotionType::Dynamic;
        inverseMass = isDynamic ? Inverse(mass) : 0.0f;
        inverseLocalInertia = isDynamic
            ? Vec3{ Inverse(localInertia.x), Inverse(localInertia.y), Inverse(localInertia.z) }
            : Vec3{};
        inverseWorldInertia = RotateDiagonal(RotationMatrix(orientation), inverseLocalInertia);

        bounds = { position, position };
        for (Collider* collider : colliders)
        {
            collider->worldBounds = ComputeBounds(*this, *collider);
            bounds = { Min(bounds.min, collider->worldBounds.min), Max(bounds.max, collider->worldBounds.max) };
        }
    }

    RigidBody* World::CreateBody(const RigidBody& description)
    {
        if (description.id == 0 || m_bodyById.contains(description.id))
        {
            return nullptr;
        }

        auto body = std::make_unique<RigidBody>(description);
        body->colliders.clear(); // colliders are attached with AddCollider
        body->force = {};
        body->torque = {};
        body->UpdateDerivedData();

        RigidBody* result = body.get();
        m_bodies.push_back(std::move(body));
        m_bodyById.emplace(result->id, result);
        return result;
    }

    Collider* World::AddCollider(RigidBody& body, const Collider& description)
    {
        if (description.id == 0 || m_colliderById.contains(description.id) || FindBody(body.id) != &body)
        {
            return nullptr;
        }

        auto collider = std::make_unique<Collider>(description);
        collider->body = &body;

        Collider* result = collider.get();
        m_colliders.push_back(std::move(collider));
        m_colliderById.emplace(result->id, result);
        body.colliders.push_back(result);
        body.UpdateDerivedData();
        return result;
    }

    DistanceJoint* World::AddDistanceJoint(const DistanceJoint& description)
    {
        const bool bodiesBelongHere = description.bodyA != nullptr && description.bodyB != nullptr
            && FindBody(description.bodyA->id) == description.bodyA
            && FindBody(description.bodyB->id) == description.bodyB;

        if (description.id == 0 || m_jointById.contains(description.id) || !bodiesBelongHere
            || description.bodyA == description.bodyB)
        {
            return nullptr;
        }

        auto joint = std::make_unique<DistanceJoint>(description);
        DistanceJoint* result = joint.get();
        m_joints.push_back(std::move(joint));
        m_jointById.emplace(result->id, result);
        return result;
    }

    RigidBody* World::FindBody(std::uint32_t id) noexcept
    {
        const auto it = m_bodyById.find(id);
        return it != m_bodyById.end() ? it->second : nullptr;
    }

    const RigidBody* World::FindBody(std::uint32_t id) const noexcept
    {
        const auto it = m_bodyById.find(id);
        return it != m_bodyById.end() ? it->second : nullptr;
    }

    void World::Step() noexcept
    {
        const float dt = fixedTimestep;

        // 1. Velocities: gravity, accumulated forces and torques, then damping.
        for (const auto& body : m_bodies)
        {
            if (body->motion != MotionType::Dynamic)
            {
                continue;
            }

            body->linearVelocity += (gravity + body->force * body->inverseMass) * dt;
            body->angularVelocity += (body->inverseWorldInertia * body->torque) * dt;
            body->linearVelocity = body->linearVelocity * (1.0f / (1.0f + dt * body->linearDamping));
            body->angularVelocity = body->angularVelocity * (1.0f / (1.0f + dt * body->angularDamping));
        }

        // 2. Positions and orientations (static bodies never move).
        for (const auto& body : m_bodies)
        {
            if (body->motion == MotionType::Static)
            {
                continue;
            }

            body->position += body->linearVelocity * dt;

            const Vec3 w = body->angularVelocity;
            const Quat spin = Quat{ 0.0f, w.x, w.y, w.z } * body->orientation;
            const Quat& q = body->orientation;
            body->orientation = Normalized({
                q.w + 0.5f * dt * spin.w, q.x + 0.5f * dt * spin.x,
                q.y + 0.5f * dt * spin.y, q.z + 0.5f * dt * spin.z });
        }

        // 3. Distance joints: project the anchors back to the rest length (mass-weighted), and
        //    remove the relative velocity along the joint axis.
        for (const auto& joint : m_joints)
        {
            RigidBody& a = *joint->bodyA;
            RigidBody& b = *joint->bodyB;
            const float totalInverseMass = a.inverseMass + b.inverseMass;
            if (totalInverseMass <= 0.0f)
            {
                continue;
            }

            const Vec3 anchorA = a.position + Rotate(a.orientation, joint->localAnchorA);
            const Vec3 anchorB = b.position + Rotate(b.orientation, joint->localAnchorB);
            const Vec3 delta = anchorB - anchorA;
            const float length = Length(delta);
            if (length <= 1e-6f)
            {
                continue;
            }

            const Vec3 axis = delta * (1.0f / length);
            const Vec3 correction = axis * ((length - joint->restLength) / totalInverseMass);
            a.position += correction * a.inverseMass;
            b.position -= correction * b.inverseMass;

            const float relativeSpeed = Dot(b.linearVelocity - a.linearVelocity, axis);
            const Vec3 impulse = axis * (relativeSpeed / totalInverseMass);
            a.linearVelocity += impulse * a.inverseMass;
            b.linearVelocity -= impulse * b.inverseMass;
        }

        // 4. Simplified contacts: a dynamic body whose bounding box sinks into the top of a static
        //    body's bounding box is pushed back up, bounces with its restitution and loses
        //    horizontal and angular speed through friction. (Stand-in for a real narrow phase.)
        for (const auto& body : m_bodies)
        {
            if (body->motion != MotionType::Dynamic || body->colliders.empty())
            {
                continue;
            }
            body->UpdateDerivedData();

            float restitution = 0.0f;
            float friction = 0.0f;
            for (const Collider* collider : body->colliders)
            {
                restitution = std::max(restitution, collider->restitution);
                friction = std::max(friction, collider->friction);
            }

            for (const auto& support : m_bodies)
            {
                if (support->motion != MotionType::Static || support->colliders.empty())
                {
                    continue;
                }

                const Aabb& a = body->bounds;
                const Aabb& b = support->bounds;
                const bool overlapsHorizontally = a.min.x < b.max.x && a.max.x > b.min.x
                    && a.min.z < b.max.z && a.max.z > b.min.z;
                const bool sinksIntoTop = a.min.y < b.max.y && a.max.y > b.max.y;
                if (!overlapsHorizontally || !sinksIntoTop)
                {
                    continue;
                }

                body->position.y += b.max.y - a.min.y;
                if (body->linearVelocity.y < 0.0f)
                {
                    body->linearVelocity.y = -body->linearVelocity.y * restitution;
                }
                const float grip = 1.0f / (1.0f + dt * 10.0f * friction);
                body->linearVelocity.x *= grip;
                body->linearVelocity.z *= grip;
                body->angularVelocity = body->angularVelocity * grip;
                body->UpdateDerivedData();
            }
        }

        // 5. End of step: derived data up to date, accumulators cleared, clock advanced.
        for (const auto& body : m_bodies)
        {
            body->force = {};
            body->torque = {};
            body->UpdateDerivedData();
        }

        ++m_stepIndex;
        m_time += static_cast<double>(dt);
    }

    void World::Clear() noexcept
    {
        m_joints.clear();
        m_colliders.clear();
        m_bodies.clear();
        m_bodyById.clear();
        m_colliderById.clear();
        m_jointById.clear();
        m_stepIndex = 0;
        m_time = 0.0;
    }
}
