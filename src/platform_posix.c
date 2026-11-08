/*
 * POSIX implementation of the tiny platform layer.
 * Linux is the primary target; the barrier uses mutex/condvar so it also
 * works on systems without pthread_barrier_t.
 */
#define _POSIX_C_SOURCE 200809L

#include "platform.h"

#include <ctype.h>
#include <pthread.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

uint64_t plat_time_ns(void)
{
    struct timespec ts;
    if (clock_gettime(CLOCK_MONOTONIC, &ts) != 0)
        return 0;
    return (uint64_t)ts.tv_sec * 1000000000ULL + (uint64_t)ts.tv_nsec;
}

int plat_logical_cores(void)
{
    long n = sysconf(_SC_NPROCESSORS_ONLN);
    return n > 0 ? (int)n : 1;
}

static char *trim(char *s)
{
    while (*s != '\0' && isspace((unsigned char)*s))
        ++s;
    char *end = s + strlen(s);
    while (end > s && isspace((unsigned char)end[-1]))
        --end;
    *end = '\0';
    return s;
}

void plat_cpu_name(char *buf, size_t n)
{
    if (n == 0)
        return;

    snprintf(buf, n, "Unknown CPU");

    FILE *f = fopen("/proc/cpuinfo", "r");
    if (f == NULL)
        return;

    char line[256];
    while (fgets(line, sizeof(line), f) != NULL) {
        if (strncmp(line, "model name", 10) == 0) {
            char *colon = strchr(line, ':');
            if (colon != NULL) {
                char *name = trim(colon + 1);
                if (*name != '\0') {
                    snprintf(buf, n, "%s", name);
                    break;
                }
            }
        }
    }
    fclose(f);
}

struct plat_thread {
    pthread_t handle;
    plat_thread_fn fn;
    void *arg;
};

static void *thread_entry(void *arg)
{
    struct plat_thread *thread = arg;
    thread->fn(thread->arg);
    return NULL;
}

plat_thread_t *plat_thread_start(plat_thread_fn fn, void *arg)
{
    if (fn == NULL)
        return NULL;

    struct plat_thread *thread = malloc(sizeof(*thread));
    if (thread == NULL)
        return NULL;

    thread->fn = fn;
    thread->arg = arg;
    if (pthread_create(&thread->handle, NULL, thread_entry, thread) != 0) {
        free(thread);
        return NULL;
    }
    return thread;
}

void plat_thread_join(plat_thread_t *thread)
{
    if (thread == NULL)
        return;
    pthread_join(thread->handle, NULL);
    free(thread);
}

struct plat_barrier {
    pthread_mutex_t mutex;
    pthread_cond_t cond;
    unsigned count;
    unsigned waiting;
    unsigned generation;
};

plat_barrier_t *plat_barrier_create(int count)
{
    if (count <= 0)
        return NULL;

    plat_barrier_t *barrier = malloc(sizeof(*barrier));
    if (barrier == NULL)
        return NULL;

    if (pthread_mutex_init(&barrier->mutex, NULL) != 0) {
        free(barrier);
        return NULL;
    }
    if (pthread_cond_init(&barrier->cond, NULL) != 0) {
        pthread_mutex_destroy(&barrier->mutex);
        free(barrier);
        return NULL;
    }

    barrier->count = (unsigned)count;
    barrier->waiting = 0;
    barrier->generation = 0;
    return barrier;
}

void plat_barrier_wait(plat_barrier_t *barrier)
{
    if (barrier == NULL)
        return;

    pthread_mutex_lock(&barrier->mutex);
    unsigned generation = barrier->generation;
    ++barrier->waiting;
    if (barrier->waiting == barrier->count) {
        barrier->waiting = 0;
        ++barrier->generation;
        pthread_cond_broadcast(&barrier->cond);
    } else {
        while (generation == barrier->generation)
            pthread_cond_wait(&barrier->cond, &barrier->mutex);
    }
    pthread_mutex_unlock(&barrier->mutex);
}

void plat_barrier_destroy(plat_barrier_t *barrier)
{
    if (barrier == NULL)
        return;
    pthread_mutex_destroy(&barrier->mutex);
    pthread_cond_destroy(&barrier->cond);
    free(barrier);
}
