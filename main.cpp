#include <raylib.h>
#include "particle_simulation.hpp"

int main()
{
    constexpr int screenWidth = 1080;
    constexpr int screenHeight = 720;

    InitWindow(screenWidth, screenHeight, "CUDA Particle Simulator");
    SetTargetFPS(60);

    // particle test{
    //     {10,10},
    //     {100,0},
    //     {0,0},
    //     10,
    //     10
    // };
    Vec2 G{0.0f, 500.0f};

    while (!WindowShouldClose())
    {
        float deltaTime = GetFrameTime();

        updateParticles(deltaTime, G, screenHeight, screenWidth);

        if (IsKeyPressed(KEY_UP))
            G = {0.0f, -500.0f};

        if (IsKeyPressed(KEY_DOWN))
            G = {0.0f, 500.0f};

        if (IsKeyPressed(KEY_LEFT))
            G = {-500.0f, 0.0f};

        if (IsKeyPressed(KEY_RIGHT))
            G = {500.0f, 0.0f};
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON))
        {
            for (int i = 0; i < 500; i++)
            {
                particle test{
                    {GetMousePosition().x + GetRandomValue(-20, 20),
                     GetMousePosition().y + GetRandomValue(-20, 20)},
                    {0, 0},
                    {0, 0},
                    7,
                    10,
                    .9};

                addParticle(test);
            }
        }

        BeginDrawing();
        ClearBackground(WHITE);
        for (const particle &test : getParticles())
        {
            DrawCircle(
                static_cast<int>(test.pos.x),
                static_cast<int>(test.pos.y),
                test.radius,
                BLUE);
        }

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
