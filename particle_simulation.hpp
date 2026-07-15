#pragma once

#include <vector>

struct Vec2 {
    float x;
    float y;
};

struct particle {
    Vec2 pos;
    Vec2 v; 
    Vec2 a;
    float radius;
    float mass;
    float e;
};

void updateParticle(particle& p, float deltaTime);
void update(particle& p, float deltaTime, Vec2 G, float height,float width);
void resetAcceleration(particle& p, float deltaTime);
void applyGravity(particle& p,float deltaTime, Vec2 G);
void floorBounce(particle& p, float height,float width);

void addParticle(const particle& p);
void updateParticles(float deltaTime, Vec2 gravity, float height, float width);
const std::vector<particle>& getParticles();
float dotProduct(Vec2 a, Vec2 b);
void ballCollision();

