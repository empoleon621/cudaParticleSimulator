#pragma once

#include "particle_simulation.hpp"

#include <cstddef>
#include <vector>

// Runs one complete frame of physics on the GPU. Particle state is copied back
// for raylib rendering after all kernels in the frame have completed.
bool updateCudaParticles(
    std::vector<particle> &particles,
    float deltaTime,
    Vec2 gravity,
    float height,
    float width,
    bool useSpatialGrid,
    std::size_t &collisionChecks);

bool isCudaRuntimeAvailable();
const char *getLastCudaError();
void shutdownCudaParticles();
