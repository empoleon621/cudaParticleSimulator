#pragma once

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
};

void updateParticle(particle& p, float deltaTime);
void update(particle& p, float deltaTime, Vec2 G, float height);
void resetAcceleration(particle& p, float deltaTime);
void applyGravity(particle& p,float deltaTime, Vec2 G);
void floorBounce(particle& p, float height);

