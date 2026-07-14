#include <raylib.h>
#include "particle_simulation.hpp"
#include <vector>

int main()
{
    constexpr int screenWidth = 1080;
    constexpr int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "CUDA Particle Simulator");
    SetTargetFPS(60);

    std::vector<particle> particleList;

    // particle test{
    //     {10,10},
    //     {100,0},
    //     {0,0},
    //     10,
    //     10
    // };
    Vec2 G{0.0f, 500.0f};

    while (!WindowShouldClose()) {
    float deltaTime = GetFrameTime();
    
    for (particle& test: particleList){
    update(test,deltaTime,G,screenHeight);
    }


    // if (IsKeyPressed(KEY_UP))
    //     G= {0.0f, -500.0f};

    // if (IsKeyPressed(KEY_DOWN))
    //     G = {0.0f, 500.0f};

    // if (IsKeyPressed(KEY_LEFT))
    //     G = {-500.0f, 0.0f};

    // if (IsKeyPressed(KEY_RIGHT))
    //     G = {500.0f, 0.0f};
    particle test{
        {GetMousePosition().x,GetMousePosition().y},
        {0,0},
        {0,0},
        10,
        10
    };

    if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON)){
        particleList.push_back(test);
    }

    BeginDrawing();
    ClearBackground(WHITE);
    for (particle test: particleList){
        DrawCircle(
        static_cast<int>(test.pos.x),
        static_cast<int>(test.pos.y),
        test.radius,
        BLUE
    );
    }

    EndDrawing();
}

    CloseWindow();
    return 0;
}
