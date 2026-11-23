#pragma once

#include <cstdint>

#include "Physics/World.h"

/*
* Example scenes, built with the engine's own API (not from a file), so that saving them exercises
* the full World -> SceneRecord -> bytes path.
*/
namespace physics_io
{
    /**
     * @brief The example scene of the demo: a static ground box, a sphere, a rotated box and a
     * capsule falling, a kinematic platform moving sideways, and a pendulum (a dynamic bob hanging
     * from a static anchor through a distance joint). Replaces the content of `world`.
     *
     * IDs: bodies 1..7, colliders 101..107, joint 201.
     */
    void BuildExampleWorld(physics::World& world);

    /**
     * @brief A reproducible scene of `bodyCount` dynamic boxes on a grid (plus one static ground),
     * with a distance joint between every pair of neighbours in a row. Used by the benchmark.
     */
    void BuildGridWorld(physics::World& world, std::uint32_t bodyCount);
}
