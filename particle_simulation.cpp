#include "particle_simulation.hpp"

void update(particle& p, float deltaTime, Vec2 G, float height){
    resetAcceleration(p,deltaTime);
    applyGravity(p,deltaTime,G);
    updateParticle(p,deltaTime);
    floorBounce(p,height);
}

void resetAcceleration(particle& p, float deltaTime){
    p.a.x = 0;
    p.a.y = 0;

}

void updateParticle(particle& p, float deltaTime)
{
    p.v.x += p.a.x * deltaTime;
    p.v.y += p.a.y * deltaTime;

    p.pos.x += p.v.x * deltaTime;
    p.pos.y += p.v.y * deltaTime;
};


//G.x exists just in case i want to have gravity not be just vertically downward
void applyGravity(particle& p,float deltaTime,Vec2 G){
    p.a.x += G.x;
    p.a.y += G.y;
}

void floorBounce(particle& p, float height){
    if (p.pos.y+p.radius >= height){
        p.pos.y = height - p.radius;
        p.v.y = p.v.y * -0.9;
    }
}
