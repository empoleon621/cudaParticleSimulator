#include "particle_simulation.hpp"

#include <algorithm>
#include <cmath>
#include <cstdint>

namespace
{
    std::vector<particle> particleList;
    bool spatialGridEnabled = true;
    bool reverseCollisionOrder = false;
    std::size_t collisionChecksLastFrame = 0;

    void resolveCollision(
        particle &a,
        particle &b,
        std::size_t indexA,
        std::size_t indexB)
    {
        Vec2 r{b.pos.x - a.pos.x, b.pos.y - a.pos.y};
        const float distSquared = r.x * r.x + r.y * r.y;
        const float sumOfRadii = a.radius + b.radius;

        if (distSquared > sumOfRadii * sumOfRadii)
        {
            return;
        }

        float dist = 0.0f;
        Vec2 norm;

        // Coincident particles have no geometric normal. Hash their indices
        // into a deterministic direction so they separate without every such
        // pair receiving the same horizontal bias.
        constexpr float minimumDistanceSquared = 0.000001f;
        if (distSquared < minimumDistanceSquared)
        {
            std::uint32_t hash = static_cast<std::uint32_t>(indexA) * 0x9E3779B9u;
            hash ^= static_cast<std::uint32_t>(indexB) * 0x85EBCA6Bu;
            hash ^= hash >> 16;
            constexpr float twoPi = 6.28318530718f;
            const float angle =
                static_cast<float>(hash & 0xFFFFu) / 65535.0f * twoPi;
            norm = {std::cos(angle), std::sin(angle)};
        }
        else
        {
            dist = std::sqrt(distSquared);
            norm = {r.x / dist, r.y / dist};
        }
        const float u1 = a.v.x * norm.x + a.v.y * norm.y;
        const float u2 = b.v.x * norm.x + b.v.y * norm.y;

        if (u1 > u2)
        {
            const float e = std::min(a.e, b.e);
            const float u1after =
                ((a.mass - b.mass * e) * u1 + (1.0f + e) * b.mass * u2) /
                (a.mass + b.mass);
            const float u2after =
                ((b.mass - a.mass * e) * u2 + (1.0f + e) * a.mass * u1) /
                (a.mass + b.mass);

            a.v.x += (u1after - u1) * norm.x;
            a.v.y += (u1after - u1) * norm.y;
            b.v.x += (u2after - u2) * norm.x;
            b.v.y += (u2after - u2) * norm.y;
        }

        // Approximate surface friction by damping relative velocity along the
        // collision tangent. Without it, identical balls slide past one
        // another too perfectly and readily form artificial-looking lattices.
        const Vec2 tangent{-norm.y, norm.x};
        const float relativeTangentVelocity =
            (b.v.x - a.v.x) * tangent.x + (b.v.y - a.v.y) * tangent.y;
        // This runs once per contact on every solver pass (up to 32 times per
        // rendered frame), so keep the per-pass value small. A larger value
        // compounds into heavy damping and makes the pile move like gelatin.
        constexpr float particleFriction = 0.01f;
        const float inverseMassA = 1.0f / a.mass;
        const float inverseMassB = 1.0f / b.mass;
        const float inverseMassSum = inverseMassA + inverseMassB;
        const float frictionImpulse =
            -relativeTangentVelocity / inverseMassSum * particleFriction;

        a.v.x -= frictionImpulse * inverseMassA * tangent.x;
        a.v.y -= frictionImpulse * inverseMassA * tangent.y;
        b.v.x += frictionImpulse * inverseMassB * tangent.x;
        b.v.y += frictionImpulse * inverseMassB * tangent.y;

        constexpr float correctionPercent = 0.8f;
        constexpr float slop = 0.01f;
        const float overlap = (sumOfRadii - dist - slop) * correctionPercent;
        if (overlap <= 0.0f)
        {
            return;
        }

        const float correctionA = overlap * inverseMassA / inverseMassSum;
        const float correctionB = overlap * inverseMassB / inverseMassSum;

        a.pos.x -= correctionA * norm.x;
        a.pos.y -= correctionA * norm.y;
        b.pos.x += correctionB * norm.x;
        b.pos.y += correctionB * norm.y;
    }
}

void update(particle &p, float deltaTime, Vec2 G, float height, float width)
{
    resetAcceleration(p, deltaTime);
    applyGravity(p, deltaTime, G);
    updateParticle(p, deltaTime);
    floorBounce(p, height, width);
}

void resetAcceleration(particle &p, float deltaTime)
{
    p.a.x = 0;
    p.a.y = 0;
}

void updateParticle(particle &p, float deltaTime)
{
    p.v.x += p.a.x * deltaTime;
    p.v.y += p.a.y * deltaTime;

    p.pos.x += p.v.x * deltaTime;
    p.pos.y += p.v.y * deltaTime;
};

// G.x exists just in case i want to have gravity not be just vertically downward
void applyGravity(particle &p, float deltaTime, Vec2 G)
{
    p.a.x += G.x;
    p.a.y += G.y;
}

void floorBounce(particle &p, float height, float width)
{
    constexpr float wallRestitution = 0.95f;
    // Only suppress tiny resting jitter; stopping at 40 px/s discarded a
    // noticeable amount of motion each time a particle touched the floor.
    constexpr float restVelocity = 5.0f;
    // Floor contact can also occur in every substep, so use gentle damping.
    constexpr float floorFriction = 0.995f;

    // floor collision
    if (p.pos.y + p.radius >= height && p.v.y > 0)
    {
        p.pos.y = height - p.radius;
        if (p.v.y < restVelocity)
        {
            p.v.y = 0.0f;
        }
        else
        {
            p.v.y *= -wallRestitution;
        }
        p.v.x *= floorFriction;
    }
    // ceiling collision
    if (p.pos.y - p.radius <= 0 && p.v.y < 0)
    {
        p.pos.y = p.radius;
        p.v.y *= -wallRestitution;
    }

    if (p.pos.x + p.radius >= width && p.v.x > 0)
    {
        p.pos.x = width - p.radius;
        p.v.x *= -wallRestitution;
    }
    if (p.pos.x - p.radius <= 0 && p.v.x < 0)
    {
        p.pos.x = p.radius;
        p.v.x *= -wallRestitution;
    }
}

void ballCollision(float height, float width)
{
    // Alternating traversal direction prevents the sequential impulse solver
    // from favoring low-index particles in the same direction every pass.
    reverseCollisionOrder = !reverseCollisionOrder;

    if (!spatialGridEnabled)
    {
        for (std::size_t order = 0; order < particleList.size(); ++order)
        {
            const std::size_t i = reverseCollisionOrder
                ? particleList.size() - 1 - order
                : order;
            for (std::size_t j = i + 1; j < particleList.size(); ++j)
            {
                ++collisionChecksLastFrame;
                resolveCollision(particleList[i], particleList[j], i, j);
            }
        }
        return;
    }

    float maxRadius = 1.0f;
    for (const particle &p : particleList)
    {
        maxRadius = std::max(maxRadius, p.radius);
    }

    // A cell is at least one maximum particle diameter wide, so overlaps can
    // only occur in the particle's own cell or one of its eight neighbors.
    const float cellSize = maxRadius * 2.0f;
    const int columns = std::max(1, static_cast<int>(std::ceil(width / cellSize)));
    const int rows = std::max(1, static_cast<int>(std::ceil(height / cellSize)));
    // Intrusive cell lists avoid one allocation-bearing vector per cell.
    std::vector<int> cellHeads(static_cast<std::size_t>(columns * rows), -1);
    std::vector<int> nextParticle(particleList.size(), -1);

    for (std::size_t i = 0; i < particleList.size(); ++i)
    {
        const int x = std::clamp(static_cast<int>(particleList[i].pos.x / cellSize), 0, columns - 1);
        const int y = std::clamp(static_cast<int>(particleList[i].pos.y / cellSize), 0, rows - 1);
        const std::size_t cell = static_cast<std::size_t>(y * columns + x);
        nextParticle[i] = cellHeads[cell];
        cellHeads[cell] = static_cast<int>(i);
    }

    for (std::size_t order = 0; order < particleList.size(); ++order)
    {
        const std::size_t i = reverseCollisionOrder
            ? particleList.size() - 1 - order
            : order;
        const int centerX = std::clamp(static_cast<int>(particleList[i].pos.x / cellSize), 0, columns - 1);
        const int centerY = std::clamp(static_cast<int>(particleList[i].pos.y / cellSize), 0, rows - 1);

        for (int y = std::max(0, centerY - 1); y <= std::min(rows - 1, centerY + 1); ++y)
        {
            for (int x = std::max(0, centerX - 1); x <= std::min(columns - 1, centerX + 1); ++x)
            {
                const std::size_t cell = static_cast<std::size_t>(y * columns + x);
                for (int candidate = cellHeads[cell]; candidate != -1;
                     candidate = nextParticle[static_cast<std::size_t>(candidate)])
                {
                    const std::size_t j = static_cast<std::size_t>(candidate);
                    if (j <= i)
                    {
                        continue;
                    }
                    ++collisionChecksLastFrame;
                    resolveCollision(particleList[i], particleList[j], i, j);
                }
            }
        }
    }
}

void addParticle(const particle &p)
{
    particleList.push_back(p);
}

void updateParticles(float deltaTime, Vec2 gravity, float height, float width)
{
    collisionChecksLastFrame = 0;

    // Window dragging and debugger pauses can produce a huge frame time.
    // Clamp it so particles cannot jump through one another in a single step.
    constexpr float maximumFrameTime = 1.0f / 30.0f;
    const float clampedDeltaTime = std::min(deltaTime, maximumFrameTime);

    // Smaller steps limit penetration. Repeating the collision solver within
    // each step makes large piles resist compression much more strongly.
    constexpr int physicsSubsteps = 8;
    constexpr int collisionIterations = 4;
    const float substepDeltaTime = clampedDeltaTime / physicsSubsteps;

    for (int step = 0; step < physicsSubsteps; ++step)
    {
        for (particle &p : particleList)
        {
            update(p, substepDeltaTime, gravity, height, width);
        }

        for (int iteration = 0; iteration < collisionIterations; ++iteration)
        {
            // Rebuild each pass because overlap correction moves particles.
            ballCollision(height, width);
        }
    }
}

const std::vector<particle> &getParticles()
{
    return particleList;
}

void setSpatialGridEnabled(bool enabled)
{
    spatialGridEnabled = enabled;
}

bool isSpatialGridEnabled()
{
    return spatialGridEnabled;
}

std::size_t getCollisionChecksLastFrame()
{
    return collisionChecksLastFrame;
}

float dotProduct(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}
