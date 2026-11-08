# Build the cpubench C11 executable. Release is the default because benchmark
# scores are only comparable when every run uses the same build settings.
cmake_minimum_required(VERSION 3.21)
project(cpubench C)

set(CMAKE_C_STANDARD 11)
set(CMAKE_C_STANDARD_REQUIRED ON)
set(CMAKE_C_EXTENSIONS OFF)

# Force -O2 instead of CMake's usual -O3 and never use -march=native or
# fast-math, so scores stay comparable between runs of the same build.
if(NOT CMAKE_BUILD_TYPE AND NOT CMAKE_CONFIGURATION_TYPES)
    set(CMAKE_BUILD_TYPE Release CACHE STRING "Build type" FORCE)
endif()

if(CMAKE_C_COMPILER_ID MATCHES "GNU|Clang")
    set(CMAKE_C_FLAGS_RELEASE "-O2 -DNDEBUG" CACHE STRING "" FORCE)
endif()

add_executable(cpubench
    src/main.c
    src/runner.c
    src/score.c
    src/workloads.c
)

if(WIN32)
    target_sources(cpubench PRIVATE src/platform_win.c)
    target_link_libraries(cpubench PRIVATE advapi32)
else()
    target_sources(cpubench PRIVATE src/platform_posix.c)
    find_package(Threads REQUIRED)
    target_link_libraries(cpubench PRIVATE Threads::Threads m)
endif()

if(MSVC)
    target_compile_options(cpubench PRIVATE /W4 /fp:precise)
    target_compile_definitions(cpubench PRIVATE _CRT_SECURE_NO_WARNINGS)
else()
    # -ffp-contract=off keeps the floating-point checksums identical across
    # compilers. It is not fast-math; it only prevents FMA contraction.
    target_compile_options(cpubench PRIVATE -Wall -Wextra -Wpedantic -ffp-contract=off)
endif()

target_compile_definitions(cpubench PRIVATE CPUBENCH_BUILD_TYPE="$<CONFIG>")
