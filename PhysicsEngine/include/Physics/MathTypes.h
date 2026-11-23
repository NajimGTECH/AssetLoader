#pragma once

#include <array>
#include <cmath>
#include <cstddef>

/*
* Math types of the physics engine (in-memory representation).
*
* Conventions: right-handed coordinates, Y up, SI units (metres, kilograms, seconds, radians).
* Quaternions are (w, x, y, z) with w the scalar part, and rotate body space into world space.
*/
namespace physics
{
    struct Vec3
    {
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        [[nodiscard]] friend constexpr Vec3 operator+(Vec3 a, Vec3 b) noexcept { return { a.x + b.x, a.y + b.y, a.z + b.z }; }
        [[nodiscard]] friend constexpr Vec3 operator-(Vec3 a, Vec3 b) noexcept { return { a.x - b.x, a.y - b.y, a.z - b.z }; }
        [[nodiscard]] friend constexpr Vec3 operator*(Vec3 a, float s) noexcept { return { a.x * s, a.y * s, a.z * s }; }
        [[nodiscard]] friend constexpr Vec3 operator*(float s, Vec3 a) noexcept { return a * s; }
        constexpr Vec3& operator+=(Vec3 other) noexcept { x += other.x; y += other.y; z += other.z; return *this; }
        constexpr Vec3& operator-=(Vec3 other) noexcept { x -= other.x; y -= other.y; z -= other.z; return *this; }

        [[nodiscard]] friend constexpr bool operator==(const Vec3&, const Vec3&) noexcept = default;
    };

    [[nodiscard]] constexpr float Dot(Vec3 a, Vec3 b) noexcept { return a.x * b.x + a.y * b.y + a.z * b.z; }

    [[nodiscard]] constexpr Vec3 Cross(Vec3 a, Vec3 b) noexcept
    {
        return { a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x };
    }

    [[nodiscard]] inline float Length(Vec3 v) noexcept { return std::sqrt(Dot(v, v)); }

    [[nodiscard]] constexpr Vec3 Abs(Vec3 v) noexcept
    {
        return { v.x < 0 ? -v.x : v.x, v.y < 0 ? -v.y : v.y, v.z < 0 ? -v.z : v.z };
    }

    struct Quat
    {
        float w = 1.0f;
        float x = 0.0f;
        float y = 0.0f;
        float z = 0.0f;

        [[nodiscard]] friend constexpr Quat operator*(Quat a, Quat b) noexcept
        {
            return {
                a.w * b.w - a.x * b.x - a.y * b.y - a.z * b.z,
                a.w * b.x + a.x * b.w + a.y * b.z - a.z * b.y,
                a.w * b.y - a.x * b.z + a.y * b.w + a.z * b.x,
                a.w * b.z + a.x * b.y - a.y * b.x + a.z * b.w,
            };
        }

        [[nodiscard]] friend constexpr bool operator==(const Quat&, const Quat&) noexcept = default;
    };

    [[nodiscard]] inline float Length(Quat q) noexcept { return std::sqrt(q.w * q.w + q.x * q.x + q.y * q.y + q.z * q.z); }

    [[nodiscard]] inline Quat Normalized(Quat q) noexcept
    {
        const float length = Length(q);
        return { q.w / length, q.x / length, q.y / length, q.z / length };
    }

    [[nodiscard]] inline Quat FromAxisAngle(Vec3 axis, float angle) noexcept
    {
        const float s = std::sin(angle * 0.5f) / Length(axis);
        return { std::cos(angle * 0.5f), axis.x * s, axis.y * s, axis.z * s };
    }

    /** @brief 3x3 matrix, row-major. */
    struct Mat3
    {
        std::array<float, 9> m{ 1, 0, 0, 0, 1, 0, 0, 0, 1 };

        [[nodiscard]] constexpr float operator()(int row, int column) const noexcept { return m[static_cast<std::size_t>(row * 3 + column)]; }
        [[nodiscard]] constexpr float& operator()(int row, int column) noexcept { return m[static_cast<std::size_t>(row * 3 + column)]; }

        [[nodiscard]] friend constexpr Vec3 operator*(const Mat3& a, Vec3 v) noexcept
        {
            return {
                a(0, 0) * v.x + a(0, 1) * v.y + a(0, 2) * v.z,
                a(1, 0) * v.x + a(1, 1) * v.y + a(1, 2) * v.z,
                a(2, 0) * v.x + a(2, 1) * v.y + a(2, 2) * v.z,
            };
        }

        [[nodiscard]] friend constexpr bool operator==(const Mat3&, const Mat3&) noexcept = default;
    };

    [[nodiscard]] constexpr Mat3 RotationMatrix(Quat q) noexcept
    {
        const float xx = q.x * q.x, yy = q.y * q.y, zz = q.z * q.z;
        const float xy = q.x * q.y, xz = q.x * q.z, yz = q.y * q.z;
        const float wx = q.w * q.x, wy = q.w * q.y, wz = q.w * q.z;
        Mat3 r;
        r.m = {
            1 - 2 * (yy + zz), 2 * (xy - wz),     2 * (xz + wy),
            2 * (xy + wz),     1 - 2 * (xx + zz), 2 * (yz - wx),
            2 * (xz - wy),     2 * (yz + wx),     1 - 2 * (xx + yy),
        };
        return r;
    }

    [[nodiscard]] constexpr Vec3 Rotate(Quat q, Vec3 v) noexcept { return RotationMatrix(q) * v; }

    /** @brief R * diag(d) * R^T: a body-space diagonal tensor expressed in world space. */
    [[nodiscard]] constexpr Mat3 RotateDiagonal(const Mat3& r, Vec3 d) noexcept
    {
        Mat3 result;
        const float diagonal[3] = { d.x, d.y, d.z };
        for (int i = 0; i < 3; ++i)
        {
            for (int j = 0; j < 3; ++j)
            {
                float sum = 0.0f;
                for (int k = 0; k < 3; ++k)
                {
                    sum += r(i, k) * diagonal[k] * r(j, k);
                }
                result(i, j) = sum;
            }
        }
        return result;
    }

    struct Aabb
    {
        Vec3 min{};
        Vec3 max{};

        [[nodiscard]] friend constexpr bool operator==(const Aabb&, const Aabb&) noexcept = default;
    };
}
