#include "particle_simulation.hpp"

namespace
{
    std::vector<particle> particleList;
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
    // floor collision
    if (p.pos.y + p.radius >= height && p.v.y > 0)
    {
        p.pos.y = height - p.radius;
        p.v.y = p.v.y * -0.9;
    }
    // ceiling collision
    if (p.pos.y - p.radius <= 0 && p.v.y < 0)
    {
        p.pos.y = p.radius;
        p.v.y = p.v.y * -0.9;
    }

    if (p.pos.x + p.radius >= width && p.v.x > 0)
    {
        p.pos.x = width - p.radius;
        p.v.x = p.v.x * -0.9;
    }
    if (p.pos.x - p.radius <= 0 && p.v.x < 0)
    {
        p.pos.x = p.radius;
        p.v.x = p.v.x * -0.9;
    }
}

void ballCollision()
{
    for (std::size_t i = 0; i < particleList.size(); i++)
    {
        for (std::size_t j = i + 1; j < particleList.size(); j++)
        {
            particle &a = particleList[i];
            particle &b = particleList[j];

            Vec2 r;
            r.x = b.pos.x - a.pos.x;
            r.y = b.pos.y - a.pos.y;

            float distSquared = r.x * r.x + r.y * r.y;

            float sumOfRadii = a.radius + b.radius;
            float sumOfRadiiSquared = sumOfRadii * sumOfRadii;

            if (distSquared <= sumOfRadiiSquared)
            {
                float dist = sqrt(distSquared);

                // Prevent division by zero
                if (dist == 0.0f)
                {
                    continue;
                }

                Vec2 norm;
                norm.x = r.x / dist;
                norm.y = r.y / dist;

                float u1 = dotProduct(a.v, norm);
                float u2 = dotProduct(b.v, norm);

                // Only change velocity if they are approaching
                if (u1 > u2)
                {
                    float e = std::min(a.e, b.e);

                    float u1after =
                        ((a.mass - b.mass * e) * u1 +
                         (1.0f + e) * b.mass * u2) /
                        (a.mass + b.mass);

                    float u2after =
                        ((b.mass - a.mass * e) * u2 +
                         (1.0f + e) * a.mass * u1) /
                        (a.mass + b.mass);

                    float deltau1 = u1after - u1;
                    float deltau2 = u2after - u2;

                    a.v.x += deltau1 * norm.x;
                    a.v.y += deltau1 * norm.y;

                    b.v.x += deltau2 * norm.x;
                    b.v.y += deltau2 * norm.y;
                }

                // Overlap correction
                float overlap = (a.radius + b.radius) - dist;

                float inverseMassA = 1.0f / a.mass;
                float inverseMassB = 1.0f / b.mass;
                float inverseMassSum = inverseMassA + inverseMassB;

                float correctionA = overlap * (inverseMassA / inverseMassSum);
                float correctionB = overlap * (inverseMassB / inverseMassSum);

                // Move a backward along the normal
                a.pos.x -= correctionA * norm.x;
                a.pos.y -= correctionA * norm.y;

                // Move b forward along the normal
                b.pos.x += correctionB * norm.x;
                b.pos.y += correctionB * norm.y;
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
    for (particle &p : particleList)
    {
        update(p, deltaTime, gravity, height, width);
    }
    ballCollision();

}

const std::vector<particle> &getParticles()
{
    return particleList;
}

float dotProduct(Vec2 a, Vec2 b)
{
    return a.x * b.x + a.y * b.y;
}
