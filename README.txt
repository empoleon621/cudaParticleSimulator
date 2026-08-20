CUDA PARTICLE SIMULATOR
=======================

OVERVIEW
--------

This project is a two-dimensional particle simulator written in C++17 with
raylib for rendering. It contains two physics backends:

1. A CPU backend implemented in particle_simulation.cpp.
2. A CUDA backend implemented in cuda_particles.cu.

Both backends can use either brute-force all-pairs collision detection or a
uniform spatial grid. The backend and broad-phase algorithm can be switched
while the application is running.


RUNTIME CONTROLS
----------------

Left mouse button
    Spawn 500 particles around the cursor.

C
    Toggle between the CPU and CUDA physics backends.

G
    Toggle between the spatial grid and brute-force all-pairs collision
    detection.

Arrow keys
    Change the direction of gravity.

Particle radius slider
    Change the radius used for newly spawned particles. Existing particles are
    not resized.

The status text displays the active backend, spatial-grid state, number of
particles, candidate-pair checks, and rendered frames per second.


REQUIREMENTS
------------

The current CUDA build was developed and tested with:

    Windows 11
    Visual Studio 2022 Build Tools
    CMake 3.31
    CUDA Toolkit 13.3.1
    NVIDIA GeForce RTX 5070 Ti
    NVIDIA driver 596.49
    raylib 6.0

The CMake configuration targets CUDA architecture 120, which is the native
architecture for the tested Blackwell GPU. Change CMAKE_CUDA_ARCHITECTURES in
CMakeLists.txt and CMakePresets.json when targeting a different GPU family.

raylib is downloaded automatically by CMake through FetchContent.


BUILDING
--------

Configure the CUDA build:

    cmake --preset cuda

Build an optimized executable:

    cmake --build --preset cuda-release --parallel

Run it:

    .\build\cuda\Release\particle_simulator.exe

Run the headless CUDA correctness test:

    .\build\cuda\Release\particle_simulator.exe --cuda-self-test

Run the benchmarks:

    .\build\cuda\RelWithDebInfo\particle_simulator.exe --benchmark


HIGH-LEVEL PHYSICS FLOW
-----------------------

Every rendered frame is divided into eight physics substeps. Each substep:

1. Applies gravity and integrates velocity and position.
2. Resolves collisions over four solver passes.
3. Applies boundary collision behavior at the floor, ceiling, and side walls.

Substeps reduce tunneling and deep penetration. Multiple solver passes make
dense piles resist compression more strongly than a single-pass solver.

Frame time is capped at 1/30 second so a paused debugger, dragged window, or
temporary stall cannot move a particle an arbitrarily large distance in one
physics update.

Coincident particles are assigned a deterministic collision normal generated
from their particle indices. This separates particles with identical centers
without imposing the same horizontal direction on every coincident pair.

Particle restitution, wall restitution, floor friction, and tangential contact
friction retain visible motion while still allowing the system to settle.


CPU BACKEND
-----------

The CPU backend uses a sequential impulse solver. In all-pairs mode it checks
every unique particle pair during every solver pass. This has O(n^2) candidate
work and quickly becomes impractical as particle count increases.

In spatial-grid mode, the CPU rebuilds a uniform grid on every solver pass.
The cell size is the diameter of the largest particle. A particle only checks
its own cell and the eight adjacent cells. Because any colliding pair must have
centers no farther apart than one maximum diameter, this neighborhood contains
all possible contacts.

The grid uses flat cell-head and next-particle arrays rather than a separate
dynamic vector for every cell. This reduces allocations and improves locality.


CUDA BACKEND
------------

The CUDA backend processes one particle per CUDA thread. Its important phases
are split into separate kernels:

1. Integrate particles and resolve window boundaries.
2. Clear and build the spatial grid when grid mode is enabled.
3. Read collision candidates and produce the next particle state.
4. Swap the current and next state buffers before the following solver pass.

CUDA operations are issued in one stream. Kernel ordering forms the global
phase boundary between grid construction and grid consumption.

The application currently copies particle state to the GPU at the start of a
physics frame and back to the CPU after the frame. The download is required
because raylib renders from CPU particle positions. Device buffers persist and
grow geometrically, so normal frames do not allocate GPU memory.

If CUDA initialization, allocation, execution, or transfer fails, the
application disables CUDA and continues with the CPU solver instead of exiting.


RACE-SAFETY DESIGN
------------------

Directly letting a collision thread modify both particles would create races:
many simultaneous pairs can share the same particle. The CUDA solver avoids
that design.

It uses Jacobi-style double buffering:

    Every thread reads an immutable input particle buffer.
    Every thread accumulates changes for only its own particle.
    Every thread writes to only its own slot in a separate output buffer.
    The input and output buffers are swapped after the kernel finishes.

Consequently, collision resolution does not need atomic operations on particle
positions or velocities.

Grid insertion is the one shared-write operation in the broad phase. Each
thread inserts its particle into a cell-linked list with atomicExch. The
collision-check counter is aggregated locally by each thread and committed with
one atomicAdd per thread. The counter is instrumentation only and does not
affect physics state.

CUDA linked-list insertion order is nondeterministic, so very small floating-
point differences can occur due to summation order. This is not a data race.

Dense clusters originally caused occasional explosive bounces because a
Jacobi thread summed several complete collision corrections into one particle.
The current solver prevents that energy injection by:

    Applying velocity impulses only on the first solver pass of each substep.
    Using later passes for positional overlap correction.
    Averaging simultaneous contact corrections per particle instead of adding
    an unbounded number of full corrections.


CORRECTNESS TESTING
-------------------

The --cuda-self-test option runs without opening a raylib window. It creates a
small deterministic set of colliding particles, alternates spatial-grid and
all-pairs CUDA modes for sixteen frames, and rejects CUDA errors or non-finite
particle state.

The Debug executable was also tested with NVIDIA Compute Sanitizer:

    compute-sanitizer --tool memcheck ... --cuda-self-test
    Result: 0 memory errors.

    compute-sanitizer --tool racecheck ... --cuda-self-test
    Result: 0 race hazards, 0 errors, and 0 warnings.


BENCHMARK METHODOLOGY
---------------------

The benchmark is built into the executable and runs headlessly with the
--benchmark option. Rendering is not included, so the measurements isolate the
complete physics update.

The reported CUDA measurements are end-to-end application timings. They
include host-to-device upload, kernel execution, synchronization, and
device-to-host download. They are not kernel-only timings.

The test state uses:

    Radius: 2 pixels
    Mass: 10
    Restitution: 0.65
    Initial center spacing: 4.5 pixels
    Physics timestep: 1/60 second
    Physics substeps per frame: 8
    Collision solver passes per substep: 4
    Total collision passes per frame: 32

Particles are placed in a deterministic dense block whose dimensions preserve
approximately the window's 4:3 aspect ratio. Small deterministic position
offsets and velocities break perfect symmetry. Every backend receives the same
initial particle state.

Normal cases use five untimed warm-up frames followed by thirty measured
frames. Warm-up excludes CUDA context creation, initial device allocation, and
cold-cache effects. The 5,000-particle all-pairs cases use one warm-up and three
measured frames because their O(n^2) cost is already extremely high.

Candidate checks are averaged over the measured frames. CUDA checks are counted
as unique logical pairs even though the Jacobi implementation evaluates each
side of a pair independently.

The benchmark compares implementations of the same simulation stages, but the
CPU uses a sequential solver while CUDA uses a race-free Jacobi solver. Their
exact floating-point trajectories are therefore not expected to be identical.


BENCHMARK RESULTS
-----------------

Hardware: NVIDIA GeForce RTX 5070 Ti
CUDA Toolkit: 13.3.1
Build: RelWithDebInfo, MSVC /O2, native sm_120 CUDA code

Particles  Backend             ms/frame   FPS equivalent   Checks/frame
---------  ------------------  ---------  ---------------  ------------
500        CPU all-pairs           7.801          128.185      3,992,000
500        CPU spatial grid        1.666          600.268         43,192
500        CUDA all-pairs          4.395          227.543      3,992,000
500        CUDA spatial grid       1.101          908.604         43,329

1,000      CPU all-pairs          29.623           33.757     15,984,000
1,000      CPU spatial grid        1.078          927.368         91,090
1,000      CUDA all-pairs          9.467          105.628     15,984,000
1,000      CUDA spatial grid       1.527          654.799         91,000

2,000      CPU all-pairs         116.867            8.557     63,968,000
2,000      CPU spatial grid        1.890          529.014        188,693
2,000      CUDA all-pairs         19.944           50.140     63,968,000
2,000      CUDA spatial grid       1.332          750.743        188,206

5,000      CPU all-pairs         714.911            1.399    399,920,000
5,000      CPU spatial grid        5.068          197.299        480,684
5,000      CUDA all-pairs         52.724           18.967    399,920,000
5,000      CUDA spatial grid       1.348          741.647        479,973

10,000     CPU all-pairs         skipped          O(n^2)               -
10,000     CPU spatial grid       10.762           92.918        976,789
10,000     CUDA all-pairs        skipped          O(n^2)               -
10,000     CUDA spatial grid      1.238          808.050        975,851

20,000     CPU all-pairs         skipped          O(n^2)               -
20,000     CPU spatial grid       17.824           56.103      2,008,106
20,000     CUDA all-pairs        skipped          O(n^2)               -
20,000     CUDA spatial grid      1.417          705.657      2,005,301

All-pairs was skipped above 5,000 particles because it no longer provides a
useful scalable comparison. At 10,000 particles it would perform approximately
1.6 billion candidate checks per frame. At 20,000 particles it would perform
approximately 6.4 billion candidate checks per frame.


FINDINGS
--------

1. Spatial partitioning matters more than moving an O(n^2) algorithm to CUDA.
   At 5,000 particles, CPU spatial grid is more than 140 times faster than CPU
   all-pairs.

2. CUDA substantially accelerates all-pairs work, but does not fix its scaling.
   At 5,000 particles, CUDA all-pairs is about 13.6 times faster than CPU
   all-pairs, yet still only reaches about 19 physics frames per second.

3. CUDA launch and transfer overhead matters at small particle counts. CPU grid
   remains highly competitive and was faster than CUDA grid at 1,000 particles
   in this run.

4. CUDA grid becomes clearly advantageous as workload increases. At 5,000
   particles it is about 3.8 times faster than CPU grid. At 10,000 it is about
   8.7 times faster. At 20,000 it is about 12.6 times faster.

5. At 20,000 particles, CUDA spatial grid completes the full eight-substep,
   thirty-two-pass physics frame in approximately 1.42 milliseconds, excluding
   rendering but including both particle transfers.

6. The spatial grid reduces candidate work by several orders of magnitude. At
   20,000 particles it performs about 2.0 million candidate checks per frame
   instead of the approximately 6.4 billion required by all-pairs.


SOURCE LAYOUT
-------------

main.cpp
    Window, input, rendering, slider UI, CUDA self-test, and benchmark harness.

particle_simulation.hpp
    Shared particle types and public simulation interface.

particle_simulation.cpp
    CPU integration, collision solver, spatial grid, and backend dispatch.

cuda_particles.hpp
    Host-facing CUDA backend interface.

cuda_particles.cu
    CUDA memory management, integration kernel, spatial-grid construction,
    race-free collision kernels, transfers, and CUDA error handling.

CMakeLists.txt
    C++/CUDA target, raylib dependency, and sm_120 configuration.

CMakePresets.json
    Repeatable Debug and Release CUDA build presets.
