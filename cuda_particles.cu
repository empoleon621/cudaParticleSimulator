#include "cuda_particles.hpp"

#include <cuda_runtime.h>

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <string>

namespace
{
    constexpr int threadsPerBlock = 256;
    constexpr int physicsSubsteps = 8;
    constexpr int collisionIterations = 4;

    struct DeviceStorage
    {
        particle *particlesA = nullptr;
        particle *particlesB = nullptr;
        int *cellHeads = nullptr;
        int *nextParticle = nullptr;
        unsigned long long *collisionChecks = nullptr;
        std::size_t particleCapacity = 0;
        std::size_t cellCapacity = 0;
    };

    DeviceStorage storage;
    std::string lastError;

    bool checkCuda(cudaError_t result, const char *operation)
    {
        if (result == cudaSuccess)
        {
            return true;
        }

        lastError = std::string(operation) + ": " + cudaGetErrorString(result);
        return false;
    }

    void freeDeviceStorage()
    {
        cudaFree(storage.particlesA);
        cudaFree(storage.particlesB);
        cudaFree(storage.cellHeads);
        cudaFree(storage.nextParticle);
        cudaFree(storage.collisionChecks);
        storage = {};
    }

    bool ensureParticleCapacity(std::size_t count)
    {
        if (count <= storage.particleCapacity)
        {
            return true;
        }

        // Grow geometrically so adding a batch does not allocate every frame.
        std::size_t newCapacity = std::max<std::size_t>(1024, storage.particleCapacity);
        while (newCapacity < count)
        {
            newCapacity *= 2;
        }

        particle *newA = nullptr;
        particle *newB = nullptr;
        int *newNext = nullptr;
        if (!checkCuda(cudaMalloc(&newA, newCapacity * sizeof(particle)), "cudaMalloc particles A") ||
            !checkCuda(cudaMalloc(&newB, newCapacity * sizeof(particle)), "cudaMalloc particles B") ||
            !checkCuda(cudaMalloc(&newNext, newCapacity * sizeof(int)), "cudaMalloc grid links"))
        {
            cudaFree(newA);
            cudaFree(newB);
            cudaFree(newNext);
            return false;
        }

        cudaFree(storage.particlesA);
        cudaFree(storage.particlesB);
        cudaFree(storage.nextParticle);
        storage.particlesA = newA;
        storage.particlesB = newB;
        storage.nextParticle = newNext;
        storage.particleCapacity = newCapacity;
        return true;
    }

    bool ensureCellCapacity(std::size_t count)
    {
        if (count <= storage.cellCapacity)
        {
            return true;
        }

        int *newHeads = nullptr;
        if (!checkCuda(cudaMalloc(&newHeads, count * sizeof(int)), "cudaMalloc grid heads"))
        {
            return false;
        }

        cudaFree(storage.cellHeads);
        storage.cellHeads = newHeads;
        storage.cellCapacity = count;
        return true;
    }

    __device__ int clampCell(int value, int maximum)
    {
        return max(0, min(value, maximum));
    }

    __global__ void integrateKernel(
        particle *particles,
        int count,
        float deltaTime,
        Vec2 gravity,
        float height,
        float width)
    {
        const int i = blockIdx.x * blockDim.x + threadIdx.x;
        if (i >= count)
        {
            return;
        }

        particle p = particles[i];
        p.a = gravity;
        p.v.x += p.a.x * deltaTime;
        p.v.y += p.a.y * deltaTime;
        p.pos.x += p.v.x * deltaTime;
        p.pos.y += p.v.y * deltaTime;

        constexpr float wallRestitution = 0.95f;
        constexpr float restVelocity = 5.0f;
        constexpr float floorFriction = 0.995f;

        if (p.pos.y + p.radius >= height && p.v.y > 0.0f)
        {
            p.pos.y = height - p.radius;
            p.v.y = p.v.y < restVelocity ? 0.0f : -p.v.y * wallRestitution;
            p.v.x *= floorFriction;
        }
        if (p.pos.y - p.radius <= 0.0f && p.v.y < 0.0f)
        {
            p.pos.y = p.radius;
            p.v.y *= -wallRestitution;
        }
        if (p.pos.x + p.radius >= width && p.v.x > 0.0f)
        {
            p.pos.x = width - p.radius;
            p.v.x *= -wallRestitution;
        }
        if (p.pos.x - p.radius <= 0.0f && p.v.x < 0.0f)
        {
            p.pos.x = p.radius;
            p.v.x *= -wallRestitution;
        }

        particles[i] = p;
    }

    __global__ void buildGridKernel(
        const particle *particles,
        int count,
        float cellSize,
        int columns,
        int rows,
        int *cellHeads,
        int *nextParticle)
    {
        const int i = blockIdx.x * blockDim.x + threadIdx.x;
        if (i >= count)
        {
            return;
        }

        const int x = clampCell(static_cast<int>(particles[i].pos.x / cellSize), columns - 1);
        const int y = clampCell(static_cast<int>(particles[i].pos.y / cellSize), rows - 1);
        const int cell = y * columns + x;

        // Many threads may insert into the same cell. atomicExch returns the
        // previous head, producing a valid lock-free linked list without races.
        nextParticle[i] = atomicExch(&cellHeads[cell], i);
    }

    __device__ Vec2 coincidentNormal(int indexA, int indexB)
    {
        const int lower = min(indexA, indexB);
        const int upper = max(indexA, indexB);
        std::uint32_t hash = static_cast<std::uint32_t>(lower) * 0x9E3779B9u;
        hash ^= static_cast<std::uint32_t>(upper) * 0x85EBCA6Bu;
        hash ^= hash >> 16;
        constexpr float twoPi = 6.28318530718f;
        const float angle = static_cast<float>(hash & 0xFFFFu) / 65535.0f * twoPi;
        Vec2 normal{cosf(angle), sinf(angle)};
        if (indexA > indexB)
        {
            normal.x = -normal.x;
            normal.y = -normal.y;
        }
        return normal;
    }

    __device__ bool accumulateCollision(
        const particle &a,
        const particle &b,
        int indexA,
        int indexB,
        bool solveVelocity,
        Vec2 &velocityDelta,
        Vec2 &positionDelta)
    {
        const Vec2 separation{b.pos.x - a.pos.x, b.pos.y - a.pos.y};
        const float distanceSquared =
            separation.x * separation.x + separation.y * separation.y;
        const float radii = a.radius + b.radius;
        if (distanceSquared > radii * radii)
        {
            return false;
        }

        float distance = 0.0f;
        Vec2 normal;
        if (distanceSquared < 0.000001f)
        {
            normal = coincidentNormal(indexA, indexB);
        }
        else
        {
            distance = sqrtf(distanceSquared);
            normal = {separation.x / distance, separation.y / distance};
        }

        if (solveVelocity)
        {
            const float velocityA = a.v.x * normal.x + a.v.y * normal.y;
            const float velocityB = b.v.x * normal.x + b.v.y * normal.y;
            if (velocityA > velocityB)
            {
                const float restitution = fminf(a.e, b.e);
                const float velocityAfter =
                    ((a.mass - b.mass * restitution) * velocityA +
                     (1.0f + restitution) * b.mass * velocityB) /
                    (a.mass + b.mass);
                const float change = velocityAfter - velocityA;
                velocityDelta.x += change * normal.x;
                velocityDelta.y += change * normal.y;
            }

            const Vec2 tangent{-normal.y, normal.x};
            const float relativeTangent =
                (b.v.x - a.v.x) * tangent.x + (b.v.y - a.v.y) * tangent.y;
            constexpr float particleFriction = 0.01f;
            const float inverseMassA = 1.0f / a.mass;
            const float inverseMassB = 1.0f / b.mass;
            const float frictionImpulse =
                -relativeTangent / (inverseMassA + inverseMassB) * particleFriction;
            velocityDelta.x -= frictionImpulse * inverseMassA * tangent.x;
            velocityDelta.y -= frictionImpulse * inverseMassA * tangent.y;
        }

        constexpr float correctionPercent = 0.8f;
        constexpr float slop = 0.01f;
        const float overlap = (radii - distance - slop) * correctionPercent;
        if (overlap > 0.0f)
        {
            const float inverseMassA = 1.0f / a.mass;
            const float inverseMassB = 1.0f / b.mass;
            const float correction =
                overlap * inverseMassA / (inverseMassA + inverseMassB);
            positionDelta.x -= correction * normal.x;
            positionDelta.y -= correction * normal.y;
        }

        return true;
    }

    __global__ void collideKernel(
        const particle *input,
        particle *output,
        int count,
        bool useSpatialGrid,
        bool solveVelocity,
        float cellSize,
        int columns,
        int rows,
        const int *cellHeads,
        const int *nextParticle,
        unsigned long long *checks)
    {
        const int i = blockIdx.x * blockDim.x + threadIdx.x;
        if (i >= count)
        {
            return;
        }

        const particle a = input[i];
        Vec2 velocityDelta{0.0f, 0.0f};
        Vec2 positionDelta{0.0f, 0.0f};
        unsigned long long localChecks = 0;
        int contactCount = 0;

        if (useSpatialGrid)
        {
            const int centerX = clampCell(static_cast<int>(a.pos.x / cellSize), columns - 1);
            const int centerY = clampCell(static_cast<int>(a.pos.y / cellSize), rows - 1);
            for (int y = max(0, centerY - 1); y <= min(rows - 1, centerY + 1); ++y)
            {
                for (int x = max(0, centerX - 1); x <= min(columns - 1, centerX + 1); ++x)
                {
                    for (int j = cellHeads[y * columns + x]; j != -1; j = nextParticle[j])
                    {
                        if (j == i)
                        {
                            continue;
                        }
                        ++localChecks;
                        if (accumulateCollision(
                                a,
                                input[j],
                                i,
                                j,
                                solveVelocity,
                                velocityDelta,
                                positionDelta))
                        {
                            ++contactCount;
                        }
                    }
                }
            }
        }
        else
        {
            for (int j = 0; j < count; ++j)
            {
                if (j == i)
                {
                    continue;
                }
                ++localChecks;
                if (accumulateCollision(
                        a,
                        input[j],
                        i,
                        j,
                        solveVelocity,
                        velocityDelta,
                        positionDelta))
                {
                    ++contactCount;
                }
            }
        }

        particle result = a;
        if (contactCount > 0)
        {
            // Jacobi threads see all contacts simultaneously. Averaging their
            // corrections prevents dense clusters from applying many full
            // impulses to one particle and injecting explosive kinetic energy.
            const float contactWeight = 1.0f / static_cast<float>(contactCount);
            result.v.x += velocityDelta.x * contactWeight;
            result.v.y += velocityDelta.y * contactWeight;
            result.pos.x += positionDelta.x * contactWeight;
            result.pos.y += positionDelta.y * contactWeight;
        }
        output[i] = result;

        // One device-wide atomic per particle avoids turning instrumentation
        // into the dominant cost of collision detection.
        atomicAdd(checks, localChecks);
    }
}

bool isCudaRuntimeAvailable()
{
    int deviceCount = 0;
    const cudaError_t result = cudaGetDeviceCount(&deviceCount);
    if (result != cudaSuccess || deviceCount == 0)
    {
        lastError = result == cudaSuccess
            ? "CUDA unavailable: no CUDA device"
            : std::string("CUDA unavailable: ") + cudaGetErrorString(result);
        // Clear the sticky runtime error so a later retry is possible.
        cudaGetLastError();
        return false;
    }

    lastError.clear();
    return true;
}

bool updateCudaParticles(
    std::vector<particle> &particles,
    float deltaTime,
    Vec2 gravity,
    float height,
    float width,
    bool useSpatialGrid,
    std::size_t &collisionChecks)
{
    collisionChecks = 0;
    if (particles.empty())
    {
        return isCudaRuntimeAvailable();
    }
    if (!isCudaRuntimeAvailable() || !ensureParticleCapacity(particles.size()))
    {
        return false;
    }

    if (storage.collisionChecks == nullptr &&
        !checkCuda(cudaMalloc(&storage.collisionChecks, sizeof(unsigned long long)),
                   "cudaMalloc collision counter"))
    {
        return false;
    }

    float maxRadius = 1.0f;
    for (const particle &p : particles)
    {
        maxRadius = std::max(maxRadius, p.radius);
    }
    const float cellSize = maxRadius * 2.0f;
    const int columns = std::max(1, static_cast<int>(std::ceil(width / cellSize)));
    const int rows = std::max(1, static_cast<int>(std::ceil(height / cellSize)));
    const std::size_t cellCount = static_cast<std::size_t>(columns * rows);
    if (useSpatialGrid && !ensureCellCapacity(cellCount))
    {
        return false;
    }

    const std::size_t particleBytes = particles.size() * sizeof(particle);
    if (!checkCuda(cudaMemcpy(
            storage.particlesA,
            particles.data(),
            particleBytes,
            cudaMemcpyHostToDevice),
        "upload particles") ||
        !checkCuda(cudaMemset(storage.collisionChecks, 0, sizeof(unsigned long long)),
                   "clear collision counter"))
    {
        return false;
    }

    particle *current = storage.particlesA;
    particle *next = storage.particlesB;
    const int count = static_cast<int>(particles.size());
    const int blocks = (count + threadsPerBlock - 1) / threadsPerBlock;
    const float clampedDeltaTime = fminf(deltaTime, 1.0f / 30.0f);
    const float substepDeltaTime = clampedDeltaTime / physicsSubsteps;

    for (int step = 0; step < physicsSubsteps; ++step)
    {
        integrateKernel<<<blocks, threadsPerBlock>>>(
            current, count, substepDeltaTime, gravity, height, width);

        for (int iteration = 0; iteration < collisionIterations; ++iteration)
        {
            if (useSpatialGrid)
            {
                if (!checkCuda(cudaMemset(
                        storage.cellHeads, 0xFF, cellCount * sizeof(int)),
                    "clear spatial grid"))
                {
                    return false;
                }
                buildGridKernel<<<blocks, threadsPerBlock>>>(
                    current,
                    count,
                    cellSize,
                    columns,
                    rows,
                    storage.cellHeads,
                    storage.nextParticle);
            }

            collideKernel<<<blocks, threadsPerBlock>>>(
                current,
                next,
                count,
                useSpatialGrid,
                iteration == 0,
                cellSize,
                columns,
                rows,
                storage.cellHeads,
                storage.nextParticle,
                storage.collisionChecks);
            std::swap(current, next);
        }
    }

    if (!checkCuda(cudaGetLastError(), "launch CUDA physics kernels"))
    {
        return false;
    }

    unsigned long long directedChecks = 0;
    if (!checkCuda(cudaMemcpy(
            particles.data(), current, particleBytes, cudaMemcpyDeviceToHost),
        "download particles") ||
        !checkCuda(cudaMemcpy(
            &directedChecks,
            storage.collisionChecks,
            sizeof(directedChecks),
            cudaMemcpyDeviceToHost),
        "download collision counter"))
    {
        return false;
    }

    // Each Jacobi thread examines its own side of a pair, so divide the
    // directed count by two to match the CPU HUD's unique-pair convention.
    collisionChecks = static_cast<std::size_t>(directedChecks / 2ULL);
    lastError.clear();
    return true;
}

const char *getLastCudaError()
{
    return lastError.c_str();
}

void shutdownCudaParticles()
{
    freeDeviceStorage();
}
