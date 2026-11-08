/*
 * Windows implementation of the tiny platform layer.
 * Uses QueryPerformanceCounter, CreateThread, and the registry for the CPU
 * name. No affinity or thread pinning is done.
 */
#ifndef _WIN32_WINNT
#define _WIN32_WINNT 0x0601
#endif

#include "platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <windows.h>

uint64_t plat_time_ns(void)
{
    static LARGE_INTEGER frequency;
    static int initialized = 0;

    if (!initialized) {
        QueryPerformanceFrequency(&frequency);
        initialized = 1;
    }

    LARGE_INTEGER counter;
    QueryPerformanceCounter(&counter);

    uint64_t ticks = (uint64_t)counter.QuadPart;
    uint64_t freq = (uint64_t)frequency.QuadPart;
    if (freq == 0)
        return 0;

    /* Split the conversion to avoid overflowing a 64-bit intermediate. */
    return (ticks / freq) * 1000000000ULL +
           ((ticks % freq) * 1000000000ULL) / freq;
}

int plat_logical_cores(void)
{
    SYSTEM_INFO info;
    GetSystemInfo(&info);
    return info.dwNumberOfProcessors > 0 ? (int)info.dwNumberOfProcessors : 1;
}

static void trim(char *s)
{
    char *start = s;
    while (*start != '\0' && (unsigned char)*start <= ' ')
        ++start;
    if (start != s)
        memmove(s, start, strlen(start) + 1);

    size_t len = strlen(s);
    while (len > 0 && (unsigned char)s[len - 1] <= ' ')
        s[--len] = '\0';
}

void plat_cpu_name(char *buf, size_t n)
{
    if (n == 0)
        return;

    snprintf(buf, n, "Unknown CPU");

    char name[256];
    DWORD size = (DWORD)sizeof(name);
    LONG rc = RegGetValueA(
        HKEY_LOCAL_MACHINE,
        "HARDWARE\\DESCRIPTION\\System\\CentralProcessor\\0",
        "ProcessorNameString",
        RRF_RT_REG_SZ,
        NULL,
        name,
        &size);

    if (rc == ERROR_SUCCESS) {
        name[sizeof(name) - 1] = '\0';
        trim(name);
        if (name[0] != '\0')
            snprintf(buf, n, "%s", name);
    }
}

struct plat_thread {
    HANDLE handle;
    plat_thread_fn fn;
    void *arg;
};

static DWORD WINAPI thread_entry(LPVOID arg)
{
    struct plat_thread *thread = arg;
    thread->fn(thread->arg);
    return 0;
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
    thread->handle = CreateThread(NULL, 0, thread_entry, thread, 0, NULL);
    if (thread->handle == NULL) {
        free(thread);
        return NULL;
    }
    return thread;
}

void plat_thread_join(plat_thread_t *thread)
{
    if (thread == NULL)
        return;
    WaitForSingleObject(thread->handle, INFINITE);
    CloseHandle(thread->handle);
    free(thread);
}

struct plat_barrier {
    SRWLOCK lock;
    CONDITION_VARIABLE cond;
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

    InitializeSRWLock(&barrier->lock);
    InitializeConditionVariable(&barrier->cond);
    barrier->count = (unsigned)count;
    barrier->waiting = 0;
    barrier->generation = 0;
    return barrier;
}

void plat_barrier_wait(plat_barrier_t *barrier)
{
    if (barrier == NULL)
        return;

    AcquireSRWLockExclusive(&barrier->lock);
    unsigned generation = barrier->generation;
    ++barrier->waiting;
    if (barrier->waiting == barrier->count) {
        barrier->waiting = 0;
        ++barrier->generation;
        WakeAllConditionVariable(&barrier->cond);
    } else {
        while (generation == barrier->generation)
            SleepConditionVariableSRW(&barrier->cond, &barrier->lock,
                                      INFINITE, 0);
    }
    ReleaseSRWLockExclusive(&barrier->lock);
}

void plat_barrier_destroy(plat_barrier_t *barrier)
{
    free(barrier);
}
