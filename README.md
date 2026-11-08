# cpubench

`cpubench` is a small, readable CPU benchmark written in C11. It runs four
fixed workloads on one thread and on all logical CPUs, then reports
Geekbench-style single-core and multi-core scores. It builds on Linux and
Windows with no external libraries.

## What it measures

| Workload | Work performed | Reported unit |
|----------|----------------|---------------|
| Integer  | Prime sieve up to 20,000,000 | numbers/s |
| Float    | Mandelbrot set, 1200 x 800 grid, 500 iterations | pixels/s |
| Memory   | 512 x 512 double matrix multiply, naive i-j-k loop | FLOP/s |
| Hashing  | SHA-256 over a 1 MiB buffer, repeated 64 times | bytes/s |

Every workload has a known checksum. If a result does not match, the tool
prints a clear `FAIL` message and exits instead of reporting a bogus score.

## Build

Requirements: a C11 compiler, CMake 3.21 or newer, and pthreads on POSIX.
There are no third-party libraries.

### Linux

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/cpubench
```

Release is the default. The build uses `-O2` and never uses `-march=native`
or fast-math, so scores from the same release build are comparable.

### Windows

From a Visual Studio Developer Command Prompt:

```bat
cmake -S . -B build
cmake --build build --config Release
build\Release\cpubench.exe
```

With MinGW:

```bat
cmake -S . -B build -G "MinGW Makefiles" -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
build\cpubench.exe
```

## Run

```sh
cpubench                    # single-core and multi-core
cpubench --single           # single-core only
cpubench --multi            # all logical CPUs
cpubench --multi --threads 8
cpubench --json result.json
```

The default multi-core thread count is the number of logical CPUs. Use
`--threads N` to override it.

Example output:

```text
CPU: AMD Ryzen 7 5700G with Radeon Graphics   Logical cores: 16   OS: Linux   Compiler: GCC 16.2.1   Build: Release
---------------------------------------------------------
Test                 Single    Multi (16T)
Integer                 967            683
Float                   992          13764
Memory                 1000           4172
Hashing                1000           8427
---------------------------------------------------------
Single-Core Score: 990
Multi-Core Score:  4263
Multi-core scaling: 4.31x
```

Scores vary a little between runs. Integer and Memory are memory-bandwidth
bound on many machines, so their multi-core scores scale less than Float and
Hashing.

## How scoring works

Each workload has a reference throughput in `src/score.c`. A reference
throughput represents the "1000 point" machine for that workload:

```text
workload score = 1000 * (measured throughput / reference throughput)
```

A CPU that is twice as fast gets about 2000 points; there is no upper limit.
The Single-Core Score is the geometric mean of the four workload scores, so no
single test dominates:

```text
score = exp((log(s1) + log(s2) + log(s3) + log(s4)) / 4)
```

For multi-core runs, every thread performs its own complete copy of the
workload with its own memory. The total work is therefore `N * work_units`,
and the multi-core throughput is `N * work_units / wall_time`. The
Multi-Core Score uses the same geometric mean, and multi-core scaling is
`multi_score / single_score`.

Each workload gets one untimed warm-up run followed by three timed runs. The
fastest timed run is used because it is the least disturbed by OS scheduling,
interrupts, and background load. The timer is monotonic and uses
`CLOCK_MONOTONIC` on POSIX and `QueryPerformanceCounter` on Windows.

## Calibration

The reference throughputs in `src/score.c` were captured on:

- AMD Ryzen 7 5700G, 16 logical / 8 physical cores
- Linux, GCC 16.2.1, Release build

This makes that machine's single-core score approximately 1000. To
re-baseline on another machine:

1. Build in Release mode.
2. Close other heavy programs and let the machine reach a steady temperature.
3. Run the hidden calibration mode:

   ```sh
   ./build/cpubench --calibrate
   ```

   It prints the raw throughput and checksum of each workload.

4. Copy the four throughput values into `reference_throughput[]` in
   `src/score.c`.
5. If you changed a workload's size or algorithm, set its `expected_checksum`
   to `0` in `src/workloads.c` before running calibration, then copy the
   newly printed checksum back after.
6. Rebuild.

A checksum of `0` in `workloads.c` means "not calibrated yet"; calibration
mode will report the value instead of failing. The checked-in project has all
four checksums filled in.

## JSON output

`--json FILE` writes a small hand-rolled JSON document:

```json
{
  "cpu": "AMD Ryzen 7 5700G with Radeon Graphics",
  "logical_cores": 16,
  "os": "Linux",
  "compiler": "GCC 16.2.1",
  "build": "Release",
  "single": {
    "score": 990,
    "workloads": {
      "integer": { "score": 967, "throughput": 231151699.706, "unit": "numbers/s" },
      "float":   { "score": 992, "throughput": 4091554.813,  "unit": "pixels/s" },
      "memory":  { "score": 1000, "throughput": 840534422.235, "unit": "FLOP/s" },
      "hashing": { "score": 1000, "throughput": 352374864.060, "unit": "bytes/s" }
    }
  },
  "multi": {
    "score": 4263,
    "threads": 16,
    "workloads": {
      "integer": { "score": 683, "throughput": 163219530.872, "unit": "numbers/s" },
      "float":   { "score": 13764, "throughput": 56775815.017, "unit": "pixels/s" },
      "memory":  { "score": 4172, "throughput": 3505557724.561, "unit": "FLOP/s" },
      "hashing": { "score": 8427, "throughput": 2969611317.690, "unit": "bytes/s" }
    }
  },
  "scaling": 4.3079
}
```

If only one mode is run, only that mode's object is present. The `scaling`
field is omitted unless both modes ran.

## Project layout

```text
CMakeLists.txt
README.md
src/
  main.c           CLI parsing, orchestration, table, JSON
  platform.h       tiny OS abstraction
  platform_posix.c clock_gettime, pthreads, mutex/condvar barrier
  platform_win.c   QueryPerformanceCounter, CreateThread, Win32 barrier
  bench.h          workload interface and registry
  workloads.c      the four workloads and SHA-256
  runner.h/.c      single-core and multi-core execution
  score.h/.c       reference throughputs and geometric mean
```

## Notes and limitations

- Scores are relative to the references in `src/score.c`; they are not an
  absolute measure of CPU performance.
- Compare numbers only from the same build flags and the same tool version.
- There is no thread pinning or affinity in v1. The OS scheduler decides where
  threads run.
- Background processes, laptop power settings, and thermal throttling can
  change results. The warm-up and best-of-three method reduces, but does not
  remove, that noise.
- Floating-point checksums assume IEEE-754 double precision with FP
  contraction disabled. The CMake build enforces this with
  `-ffp-contract=off` on GCC/Clang and `/fp:precise` on MSVC; fast-math and
  `-march=native` are deliberately not used.
