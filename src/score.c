/*
 * Geekbench-style scoring. Each workload has a reference throughput equal to
 * the 1000-point machine; the final score is the geometric mean so no single
 * workload dominates.
 */
#include "score.h"

#include "bench.h"

#include <math.h>
#include <stddef.h>

/* Work units per second on the reference machine (AMD Ryzen 7 5700G,
 * Linux, GCC 16.2.1, Release). Re-run `cpubench --calibrate` to update. */
static const double reference_throughput[BENCH_WORKLOAD_COUNT] = {
    239043995.175, /* integer: numbers/s */
    4125056.094,   /* float: pixels/s */
    840316146.160, /* memory: FLOP/s */
    352376891.943  /* hashing: bytes/s */
};

double score_from_throughput(int workload_index, double throughput)
{
    if (workload_index < 0 || workload_index >= BENCH_WORKLOAD_COUNT)
        return 0.0;

    double reference = reference_throughput[workload_index];
    if (reference <= 0.0 || throughput <= 0.0)
        return 0.0;

    return 1000.0 * throughput / reference;
}

double score_geomean(const double *scores, int count)
{
    if (scores == NULL || count <= 0)
        return 0.0;

    double sum = 0.0;
    for (int i = 0; i < count; ++i) {
        if (scores[i] <= 0.0)
            return 0.0;
        sum += log(scores[i]);
    }
    return exp(sum / (double)count);
}
