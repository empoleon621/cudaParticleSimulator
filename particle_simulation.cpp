#include "particle_simulation.hpp"

namespace {
std::vector<particle> particleList;
}

void update(particle& p, float deltaTime, Vec2 G, float height,float width){
    resetAcceleration(p,deltaTime);
    applyGravity(p,deltaTime,G);
    updateParticle(p,deltaTime);
    floorBounce(p,height,width);
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

void floorBounce(particle& p, float height, float width){
    // floor collision
    if (p.pos.y+p.radius >= height && p.v.y > 0){
        p.pos.y = height - p.radius;
        p.v.y = p.v.y * -0.9;
    }
    //ceiling collision
    if (p.pos.y-p.radius <= 0 && p.v.y < 0){
        p.pos.y = p.radius;
        p.v.y = p.v.y * -0.9;
    }

    if (p.pos.x+p.radius >= width && p.v.x > 0){
        p.pos.x = width - p.radius;
        p.v.x = p.v.x * -0.9;
    }
    if (p.pos.x-p.radius <= 0 && p.v.x < 0){
        p.pos.x = p.radius;
        p.v.x = p.v.x * -0.9;
    }
}

// void ballCollision(particle& a, particle &b){
//     if a.
// }

void addParticle(const particle& p){
    particleList.push_back(p);
}

void updateParticles(float deltaTime, Vec2 gravity, float height, float width){
    for (particle& p : particleList){
        update(p, deltaTime, gravity, height, width);
    }
}

const std::vector<particle>& getParticles(){
    return particleList;
}
