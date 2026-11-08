/* Single-core and multi-core execution API. */
#ifndef CPUBENCH_RUNNER_H
#define CPUBENCH_RUNNER_H

#include "bench.h"

typedef struct bench_result {
    double throughput; /* work units per second */
    double seconds;    /* best timed run */
    uint64_t checksum;
} bench_result;

int run_single(const bench_workload *workload, bench_result *result);
int run_multi(const bench_workload *workload, int threads, bench_result *result);

#endif
