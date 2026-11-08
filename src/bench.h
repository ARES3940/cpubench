/* Workload interface and the fixed registry of four benchmarks. */
#ifndef CPUBENCH_BENCH_H
#define CPUBENCH_BENCH_H

#include <stdint.h>

#define BENCH_WORKLOAD_COUNT 4

typedef struct bench_workload {
    const char *id;
    const char *name;
    const char *unit;
    uint64_t work_units;
    uint64_t expected_checksum;
    void *(*create)(void);
    void (*destroy)(void *state);
    uint64_t (*run)(void *state);
} bench_workload;

extern const bench_workload bench_workloads[BENCH_WORKLOAD_COUNT];

#endif
