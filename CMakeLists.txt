cmake_minimum_required(VERSION 3.24)

project(cuda_particle_simulator LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)

include(FetchContent)

set(BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
FetchContent_Declare(
    raylib
    GIT_REPOSITORY https://github.com/raysan5/raylib.git
    GIT_TAG 6.0
    GIT_SHALLOW TRUE
)
FetchContent_MakeAvailable(raylib)

add_executable(particle_simulator
    main.cpp
    particle_simulation.cpp
)

target_include_directories(particle_simulator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(particle_simulator PRIVATE raylib)
