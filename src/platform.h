/* Tiny OS abstraction: timer, CPU info, threads, and a reusable barrier. */
#ifndef CPUBENCH_PLATFORM_H
#define CPUBENCH_PLATFORM_H

#include <stddef.h>
#include <stdint.h>

/* Monotonic high-resolution time in nanoseconds. */
uint64_t plat_time_ns(void);

/* Number of logical CPUs available, at least 1. */
int plat_logical_cores(void);

/* Best-effort CPU name; always NUL-terminates the buffer. */
void plat_cpu_name(char *buf, size_t n);

/* Opaque handles keep the OS types out of the benchmark code. */
typedef struct plat_thread plat_thread_t;
typedef void (*plat_thread_fn)(void *arg);

plat_thread_t *plat_thread_start(plat_thread_fn fn, void *arg);
void plat_thread_join(plat_thread_t *thread);

/* Reusable barrier: every one of `count` participants must call wait(). */
typedef struct plat_barrier plat_barrier_t;

plat_barrier_t *plat_barrier_create(int count);
void plat_barrier_wait(plat_barrier_t *barrier);
void plat_barrier_destroy(plat_barrier_t *barrier);

#endif
