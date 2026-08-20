#include <raylib.h>
#include "cuda_particles.hpp"
#include "particle_simulation.hpp"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstring>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>

namespace
{
    struct BenchmarkResult
    {
        double millisecondsPerFrame;
        double framesPerSecond;
        std::size_t checksPerFrame;
    };

    std::vector<particle> createBenchmarkParticles(int count)
    {
        std::vector<particle> particles;
        particles.reserve(static_cast<std::size_t>(count));

        constexpr float particleRadius = 2.0f;
        constexpr float spacing = 4.5f;
        // Preserve roughly the window's aspect ratio as the test scales. At
        // 20k particles this occupies most of the screen without starting in
        // an overlapped state; smaller cases remain intentionally dense.
        const int columns = static_cast<int>(
            std::ceil(std::sqrt(static_cast<float>(count) * 4.0f / 3.0f)));
        for (int i = 0; i < count; ++i)
        {
            const int column = i % columns;
            const int row = i / columns;
            // Deterministic offsets and velocities break perfect symmetry while
            // ensuring every backend starts from exactly the same state.
            const float jitterX = static_cast<float>((i * 17) % 11 - 5) * 0.025f;
            const float jitterY = static_cast<float>((i * 29) % 13 - 6) * 0.025f;
            particles.push_back({
                {100.0f + column * spacing + jitterX,
                 60.0f + row * spacing + jitterY},
                {static_cast<float>((i * 7) % 31 - 15),
                 static_cast<float>((i * 11) % 29 - 14)},
                {},
                particleRadius,
                10.0f,
                0.65f});
        }
        return particles;
    }

    BenchmarkResult runBenchmarkCase(
        const std::vector<particle> &initialParticles,
        bool useCuda,
        bool useSpatialGrid,
        int warmupFrames,
        int measuredFrames)
    {
        clearParticles();
        for (const particle &p : initialParticles)
        {
            addParticle(p);
        }
        setSpatialGridEnabled(useSpatialGrid);
        setCudaEnabled(useCuda);

        constexpr Vec2 gravity{0.0f, 500.0f};
        constexpr float deltaTime = 1.0f / 60.0f;
        constexpr float height = 720.0f;
        constexpr float width = 1080.0f;

        // Warm-up excludes CUDA context creation, allocation, and cold caches.
        for (int frame = 0; frame < warmupFrames; ++frame)
        {
            updateParticles(deltaTime, gravity, height, width);
        }

        std::size_t totalChecks = 0;
        const auto start = std::chrono::steady_clock::now();
        for (int frame = 0; frame < measuredFrames; ++frame)
        {
            updateParticles(deltaTime, gravity, height, width);
            totalChecks += getCollisionChecksLastFrame();
        }
        const auto end = std::chrono::steady_clock::now();

        const double elapsedMilliseconds =
            std::chrono::duration<double, std::milli>(end - start).count();
        const double millisecondsPerFrame = elapsedMilliseconds / measuredFrames;
        return {
            millisecondsPerFrame,
            1000.0 / millisecondsPerFrame,
            totalChecks / static_cast<std::size_t>(measuredFrames)};
    }

    int runBenchmarks()
    {
        if (!isCudaAvailable())
        {
            std::cerr << "CUDA benchmark unavailable: " << getLastCudaError() << '\n';
            return 1;
        }

        constexpr int particleCounts[]{500, 1000, 2000, 5000, 10000, 20000};
        std::cout << "End-to-end physics benchmark (8 substeps, 4 solver passes)\n";
        std::cout << "CUDA timings include host/device particle transfers.\n\n";
        std::cout << std::left
                  << std::setw(11) << "Particles"
                  << std::setw(20) << "Backend"
                  << std::right
                  << std::setw(14) << "ms/frame"
                  << std::setw(14) << "FPS equiv."
                  << std::setw(18) << "checks/frame" << '\n';

        for (const int count : particleCounts)
        {
            const std::vector<particle> initialParticles =
                createBenchmarkParticles(count);
            struct Mode
            {
                const char *name;
                bool cuda;
                bool grid;
            };
            constexpr Mode modes[]{
                {"CPU all-pairs", false, false},
                {"CPU spatial grid", false, true},
                {"CUDA all-pairs", true, false},
                {"CUDA spatial grid", true, true}};

            for (const Mode &mode : modes)
            {
                // Beyond 5k, all-pairs performs at least 400 million candidate
                // checks per frame and no longer provides useful information.
                // The scalable grid implementations remain directly compared.
                if (count > 5000 && !mode.grid)
                {
                    std::cout << std::left
                              << std::setw(11) << count
                              << std::setw(20) << mode.name
                              << std::right
                              << std::setw(14) << "skipped"
                              << std::setw(14) << "O(n^2)"
                              << std::setw(18) << "-" << '\n';
                    continue;
                }

                const int measuredFrames = (!mode.grid && count == 5000) ? 3 : 30;
                const int warmupFrames = (!mode.grid && count == 5000) ? 1 : 5;
                const BenchmarkResult result = runBenchmarkCase(
                    initialParticles,
                    mode.cuda,
                    mode.grid,
                    warmupFrames,
                    measuredFrames);
                if (mode.cuda && !isCudaEnabled())
                {
                    std::cerr << "CUDA failed during benchmark: "
                              << getLastCudaError() << '\n';
                    return 2;
                }

                std::cout << std::left
                          << std::setw(11) << count
                          << std::setw(20) << mode.name
                          << std::right << std::fixed << std::setprecision(3)
                          << std::setw(14) << result.millisecondsPerFrame
                          << std::setw(14) << result.framesPerSecond
                          << std::setw(18) << result.checksPerFrame << '\n';
            }
        }

        clearParticles();
        shutdownCudaParticles();
        return 0;
    }

    int runCudaSelfTest()
    {
        std::vector<particle> particles{
            {{100.0f, 100.0f}, {30.0f, 0.0f}, {}, 4.0f, 10.0f, 0.65f},
            {{107.0f, 100.0f}, {-30.0f, 0.0f}, {}, 4.0f, 10.0f, 0.65f},
            {{300.0f, 200.0f}, {0.0f, 20.0f}, {}, 4.0f, 10.0f, 0.65f},
            {{300.0f, 207.0f}, {0.0f, -20.0f}, {}, 4.0f, 10.0f, 0.65f}};

        std::size_t checks = 0;
        for (int frame = 0; frame < 16; ++frame)
        {
            // Exercise both CUDA broad phases in the same test run.
            const bool useSpatialGrid = (frame % 2) == 0;
            if (!updateCudaParticles(
                    particles,
                    1.0f / 60.0f,
                    {0.0f, 500.0f},
                    720.0f,
                    1080.0f,
                    useSpatialGrid,
                    checks))
            {
                std::cerr << getLastCudaError() << '\n';
                shutdownCudaParticles();
                return 1;
            }
        }

        for (const particle &p : particles)
        {
            if (!std::isfinite(p.pos.x) || !std::isfinite(p.pos.y) ||
                !std::isfinite(p.v.x) || !std::isfinite(p.v.y))
            {
                std::cerr << "CUDA self-test produced non-finite state\n";
                shutdownCudaParticles();
                return 2;
            }
        }

        shutdownCudaParticles();
        std::cout << "CUDA self-test passed\n";
        return 0;
    }
}

int main(int argc, char **argv)
{
    if (argc > 1 && std::strcmp(argv[1], "--cuda-self-test") == 0)
    {
        return runCudaSelfTest();
    }
    if (argc > 1 && std::strcmp(argv[1], "--benchmark") == 0)
    {
        return runBenchmarks();
    }

    constexpr int screenWidth = 1080;
    constexpr int screenHeight = 720;
    constexpr float minimumParticleRadius = 1.0f;
    constexpr float maximumParticleRadius = 12.0f;
    constexpr Rectangle radiusSliderTrack{10.0f, 48.0f, 240.0f, 6.0f};
    constexpr Rectangle radiusSliderHitbox{8.0f, 34.0f, 244.0f, 34.0f};

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
    float particleRadius = 4.0f;
    bool draggingRadiusSlider = false;

    while (!WindowShouldClose())
    {
        float deltaTime = GetFrameTime();
        const Vector2 mouse = GetMousePosition();

        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
            CheckCollisionPointRec(mouse, radiusSliderHitbox))
        {
            draggingRadiusSlider = true;
        }
        if (IsMouseButtonReleased(MOUSE_LEFT_BUTTON))
        {
            draggingRadiusSlider = false;
        }
        if (draggingRadiusSlider)
        {
            const float sliderAmount = std::clamp(
                (mouse.x - radiusSliderTrack.x) / radiusSliderTrack.width,
                0.0f,
                1.0f);
            particleRadius = minimumParticleRadius +
                sliderAmount * (maximumParticleRadius - minimumParticleRadius);
        }

        if (IsKeyPressed(KEY_G))
        {
            setSpatialGridEnabled(!isSpatialGridEnabled());
        }

        if (IsKeyPressed(KEY_C))
        {
            setCudaEnabled(!isCudaEnabled());
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
        if (IsMouseButtonPressed(MOUSE_LEFT_BUTTON) &&
            !CheckCollisionPointRec(mouse, radiusSliderHitbox))
        {
            constexpr int particlesToSpawn = 500;

            // Spawn a non-overlapping block centered on the mouse. Previously,
            // hundreds of balls began in a tiny random area, creating severe
            // penetration before the physics solver even ran once.
            const float spawnSpacing = particleRadius * 2.2f;
            // This is smaller than half the spacing gap, so neighboring balls
            // stay separated while the perfect lattice symmetry is broken.
            const float spawnJitter = particleRadius * 0.08f;
            constexpr int spawnColumns = 25;
            constexpr int spawnRows =
                (particlesToSpawn + spawnColumns - 1) / spawnColumns;
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
        const std::string backend = isCudaEnabled() ? "CUDA" : "CPU";
        const std::string stats =
            backend + " (C: toggle)  |  " + mode + " (G: toggle)  |  Particles: " + std::to_string(getParticles().size()) +
            "  |  Pair checks: " + std::to_string(getCollisionChecksLastFrame()) +
            "  |  FPS: " + std::to_string(GetFPS());
        DrawText(stats.c_str(), 10, 10, 18, DARKGRAY);

        DrawRectangleRec(radiusSliderTrack, LIGHTGRAY);
        const float radiusSliderAmount =
            (particleRadius - minimumParticleRadius) /
            (maximumParticleRadius - minimumParticleRadius);
        const float knobX =
            radiusSliderTrack.x + radiusSliderAmount * radiusSliderTrack.width;
        DrawCircleV({knobX, radiusSliderTrack.y + radiusSliderTrack.height * 0.5f}, 8.0f, DARKBLUE);
        const std::string radiusLabel =
            "New particle radius: " + std::to_string(static_cast<int>(std::round(particleRadius)));
        DrawText(radiusLabel.c_str(), 265, 40, 18, DARKGRAY);

        EndDrawing();
    }

    shutdownCudaParticles();
    CloseWindow();
    return 0;
}
