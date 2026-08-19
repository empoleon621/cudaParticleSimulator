#include <raylib.h>
#include "particle_simulation.hpp"

#include <algorithm>
#include <string>

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

        if (IsKeyPressed(KEY_G))
        {
            setSpatialGridEnabled(!isSpatialGridEnabled());
        }

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
            constexpr int particlesToSpawn = 500;
            constexpr float particleRadius = 4.0f;

            // Spawn a non-overlapping block centered on the mouse. Previously,
            // hundreds of balls began in a tiny random area, creating severe
            // penetration before the physics solver even ran once.
            constexpr float spawnSpacing = particleRadius * 2.2f;
            // This is smaller than half the spacing gap, so neighboring balls
            // stay separated while the perfect lattice symmetry is broken.
            constexpr float spawnJitter = particleRadius * 0.08f;
            constexpr int spawnColumns = 25;
            constexpr int spawnRows =
                (particlesToSpawn + spawnColumns - 1) / spawnColumns;
            const Vector2 mouse = GetMousePosition();
            const float spawnWidth = (spawnColumns - 1) * spawnSpacing;
            const float spawnHeight = (spawnRows - 1) * spawnSpacing;
            // Shift the complete block inside the window. Clamping particles
            // individually would stack several at the same position near walls.
            const float spawnOriginX = std::clamp(
                mouse.x - spawnWidth * 0.5f,
                particleRadius + spawnJitter,
                screenWidth - particleRadius - spawnWidth - spawnJitter);
            const float spawnOriginY = std::clamp(
                mouse.y - spawnHeight * 0.5f,
                particleRadius + spawnJitter,
                screenHeight - particleRadius - spawnHeight - spawnJitter);

            for (int i = 0; i < particlesToSpawn; ++i)
            {
                const int column = i % spawnColumns;
                const int row = i / spawnColumns;
                const float jitterX =
                    GetRandomValue(-1000, 1000) / 1000.0f * spawnJitter;
                const float jitterY =
                    GetRandomValue(-1000, 1000) / 1000.0f * spawnJitter;
                const float x = spawnOriginX + column * spawnSpacing + jitterX;
                const float y = spawnOriginY + row * spawnSpacing + jitterY;

                particle test{
                    {x, y},
                    {0, 0},
                    {0, 0},
                    particleRadius,
                    10,
                    // Retain enough collision energy for dramatic rearranging,
                    // while still allowing the pile to settle eventually.
                    .65f};

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

        const std::string mode = isSpatialGridEnabled() ? "Spatial grid: ON" : "Spatial grid: OFF (brute force)";
        const std::string stats =
            mode + "  |  G: toggle  |  Particles: " + std::to_string(getParticles().size()) +
            "  |  Pair checks: " + std::to_string(getCollisionChecksLastFrame()) +
            "  |  FPS: " + std::to_string(GetFPS());
        DrawText(stats.c_str(), 10, 10, 18, DARKGRAY);

        EndDrawing();
    }

    CloseWindow();
    return 0;
}
