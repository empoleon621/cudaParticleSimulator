cmake_minimum_required(VERSION 3.24)

project(cuda_particle_simulator LANGUAGES CXX CUDA)

set(CMAKE_CXX_STANDARD 17)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_CXX_EXTENSIONS OFF)
set(CMAKE_CUDA_STANDARD 17)
set(CMAKE_CUDA_STANDARD_REQUIRED ON)
set(CMAKE_CUDA_EXTENSIONS OFF)

# RTX 50-series / Blackwell native code. CUDA 12.8 or newer is required for
# this architecture; the setup uses CUDA Toolkit 13.3.
set(CMAKE_CUDA_ARCHITECTURES 120)

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
    cuda_particles.cu
)

target_include_directories(particle_simulator PRIVATE
    ${CMAKE_CURRENT_SOURCE_DIR}
)

target_link_libraries(particle_simulator PRIVATE raylib)

# All kernels live in one translation unit, so relocatable device code adds no
# value and would only increase link time.
set_target_properties(particle_simulator PROPERTIES
    CUDA_SEPARABLE_COMPILATION OFF
)
