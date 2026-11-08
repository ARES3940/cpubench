/*
 * Runs one workload either on the calling thread or on N independent copies.
 * One warm-up run is discarded; the best of three timed runs is used because
 * the fastest run is the least disturbed by OS scheduling and background load.
 */
#include "runner.h"

#include "platform.h"

#include <inttypes.h>
#include <stdio.h>
#include <stdlib.h>

#define WARMUP_RUNS 1
#define TIMED_RUNS  3

static int verify_checksum(const bench_workload *workload, uint64_t checksum)
{
    /* A zero placeholder means the workload has not been calibrated yet;
     * --calibrate uses that state to report the first checksums. */
    if (workload->expected_checksum == 0)
        return 0;

    if (checksum == workload->expected_checksum)
        return 0;

    fprintf(stderr,
            "FAIL: %s checksum 0x%016" PRIx64 " (expected 0x%016" PRIx64 ")\n",
            workload->name, checksum, workload->expected_checksum);
    return -1;
}

int run_single(const bench_workload *workload, bench_result *result)
{
    void *state = workload->create();
    if (state == NULL) {
        fprintf(stderr, "error: out of memory preparing %s\n", workload->name);
        return -1;
    }

    for (int i = 0; i < WARMUP_RUNS; ++i)
        (void)workload->run(state);

    uint64_t best = UINT64_MAX;
    uint64_t checksum = 0;
    for (int i = 0; i < TIMED_RUNS; ++i) {
        uint64_t start = plat_time_ns();
        checksum = workload->run(state);
        uint64_t elapsed = plat_time_ns() - start;
        if (elapsed < best)
            best = elapsed;
    }

    workload->destroy(state);

    if (verify_checksum(workload, checksum) != 0)
        return -1;
    if (best == 0) {
        fprintf(stderr, "error: timer resolution too low for %s\n",
                workload->name);
        return -1;
    }

    result->seconds = (double)best / 1000000000.0;
    result->throughput = (double)workload->work_units / result->seconds;
    result->checksum = checksum;
    return 0;
}

typedef struct {
    const bench_workload *workload;
    void *state;
    uint64_t checksum;
    plat_barrier_t *barrier;
} worker_context;

static void worker_main(void *opaque)
{
    worker_context *context = opaque;

    (void)context->workload->run(context->state); /* warm-up */

    /* Wait until every worker has finished warming up before the timed rounds
     * begin, so the first measurement does not include another thread's
     * warm-up tail. */
    plat_barrier_wait(context->barrier);

    for (int round = 0; round < TIMED_RUNS; ++round) {
        plat_barrier_wait(context->barrier); /* start */
        context->checksum = context->workload->run(context->state);
        plat_barrier_wait(context->barrier); /* done */
    }
}

int run_multi(const bench_workload *workload, int threads, bench_result *result)
{
    if (threads < 1) {
        fprintf(stderr, "error: thread count must be at least 1\n");
        return -1;
    }

    void **states = calloc((size_t)threads, sizeof(*states));
    worker_context *contexts = calloc((size_t)threads, sizeof(*contexts));
    plat_thread_t **handles = calloc((size_t)threads, sizeof(*handles));
    if (states == NULL || contexts == NULL || handles == NULL) {
        fprintf(stderr, "error: out of memory preparing multi-core run\n");
        free(states);
        free(contexts);
        free(handles);
        return -1;
    }

    for (int i = 0; i < threads; ++i) {
        states[i] = workload->create();
        if (states[i] == NULL) {
            fprintf(stderr, "error: out of memory preparing %s worker %d\n",
                    workload->name, i);
            for (int j = 0; j < i; ++j)
                workload->destroy(states[j]);
            free(states);
            free(contexts);
            free(handles);
            return -1;
        }
        contexts[i].workload = workload;
        contexts[i].state = states[i];
        contexts[i].checksum = 0;
    }

    plat_barrier_t *barrier = plat_barrier_create(threads + 1);
    if (barrier == NULL) {
        fprintf(stderr, "error: could not create start barrier\n");
        for (int i = 0; i < threads; ++i)
            workload->destroy(states[i]);
        free(states);
        free(contexts);
        free(handles);
        return -1;
    }

    for (int i = 0; i < threads; ++i) {
        contexts[i].barrier = barrier;
        handles[i] = plat_thread_start(worker_main, &contexts[i]);
        if (handles[i] == NULL) {
            /* Some workers may already be waiting at the barrier. Exiting is
             * safer than trying to unwind a partially started team. */
            fprintf(stderr, "error: could not start %s worker %d\n",
                    workload->name, i);
            exit(EXIT_FAILURE);
        }
    }

    plat_barrier_wait(barrier); /* all warm-ups finished */

    uint64_t best = UINT64_MAX;
    for (int round = 0; round < TIMED_RUNS; ++round) {
        uint64_t start = plat_time_ns();
        plat_barrier_wait(barrier); /* start */
        plat_barrier_wait(barrier); /* done */
        uint64_t elapsed = plat_time_ns() - start;
        if (elapsed < best)
            best = elapsed;
    }

    for (int i = 0; i < threads; ++i)
        plat_thread_join(handles[i]);
    plat_barrier_destroy(barrier);

    int failed = 0;
    for (int i = 0; i < threads; ++i) {
        if (verify_checksum(workload, contexts[i].checksum) != 0) {
            fprintf(stderr, "      (worker %d of %d)\n", i + 1, threads);
            failed = 1;
        }
    }

    uint64_t checksum = contexts[0].checksum;
    for (int i = 0; i < threads; ++i)
        workload->destroy(states[i]);
    free(states);
    free(contexts);
    free(handles);

    if (failed)
        return -1;
    if (best == 0) {
        fprintf(stderr, "error: timer resolution too low for %s\n",
                workload->name);
        return -1;
    }

    result->seconds = (double)best / 1000000000.0;
    /* Each thread performed a complete copy of the workload. */
    result->throughput =
        (double)threads * (double)workload->work_units / result->seconds;
    result->checksum = checksum;
    return 0;
}
