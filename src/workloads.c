/*
 * The four benchmark workloads. Every workload owns its working memory and
 * fills it with a fixed xorshift64 stream, so each thread's copy is identical
 * and one expected checksum is valid for every copy.
 */
#include "bench.h"

#include <stdint.h>
#include <stdlib.h>
#include <string.h>

#define FNV_OFFSET 1469598103934665603ULL
#define FNV_PRIME  1099511628211ULL

static uint64_t fnv_u64(uint64_t hash, uint64_t value)
{
    for (int i = 0; i < 8; ++i) {
        hash ^= (value >> (i * 8)) & 0xffULL;
        hash *= FNV_PRIME;
    }
    return hash;
}

static uint64_t rng_next(uint64_t *state)
{
    uint64_t x = *state;
    x ^= x << 13;
    x ^= x >> 7;
    x ^= x << 17;
    *state = x;
    return x;
}

/* ---------------------------------------------------------------- integer */

#define SIEVE_LIMIT 20000000u

typedef struct {
    uint8_t *composite;
} sieve_state;

static void *sieve_create(void)
{
    sieve_state *state = malloc(sizeof(*state));
    if (state == NULL)
        return NULL;

    state->composite = calloc((size_t)SIEVE_LIMIT + 1, 1);
    if (state->composite == NULL) {
        free(state);
        return NULL;
    }
    return state;
}

static void sieve_destroy(void *opaque)
{
    sieve_state *state = opaque;
    if (state == NULL)
        return;
    free(state->composite);
    free(state);
}

static uint64_t sieve_run(void *opaque)
{
    sieve_state *state = opaque;
    uint8_t *composite = state->composite;
    const uint32_t limit = SIEVE_LIMIT;

    /* Clearing is part of the measured work; the buffer itself was allocated
     * before timing started. */
    memset(composite, 0, (size_t)limit + 1);
    composite[0] = 1;
    composite[1] = 1;

    for (uint32_t i = 2; (uint64_t)i * i <= limit; ++i) {
        if (composite[i] == 0) {
            for (uint64_t j = (uint64_t)i * i; j <= limit; j += i)
                composite[j] = 1;
        }
    }

    uint64_t hash = FNV_OFFSET;
    for (uint32_t i = 2; i <= limit; ++i) {
        if (composite[i] == 0)
            hash = fnv_u64(hash, i);
    }
    return hash;
}

/* ------------------------------------------------------------------ float */

#define MB_WIDTH    1200
#define MB_HEIGHT   800
#define MB_MAX_ITER 500

static void *mandel_create(void)
{
    return malloc(1); /* a non-NULL token; this workload needs no data */
}

static void mandel_destroy(void *opaque)
{
    free(opaque);
}

static uint64_t mandel_run(void *opaque)
{
    (void)opaque;

    uint64_t hash = FNV_OFFSET;
    for (int y = 0; y < MB_HEIGHT; ++y) {
        double ci = -1.2 + 2.4 * (double)y / (double)(MB_HEIGHT - 1);
        for (int x = 0; x < MB_WIDTH; ++x) {
            double cr = -2.2 + 3.0 * (double)x / (double)(MB_WIDTH - 1);
            double zr = 0.0;
            double zi = 0.0;
            int iter = 0;

            while (iter < MB_MAX_ITER) {
                double zr2 = zr * zr;
                double zi2 = zi * zi;
                if (zr2 + zi2 > 4.0)
                    break;
                zi = 2.0 * zr * zi + ci;
                zr = zr2 - zi2 + cr;
                ++iter;
            }
            hash = fnv_u64(hash, (uint64_t)iter);
        }
    }
    return hash;
}

/* ----------------------------------------------------------------- memory */

#define MATRIX_N 512

typedef struct {
    double *a;
    double *b;
    double *c;
} matrix_state;

static void *matrix_create(void)
{
    matrix_state *state = malloc(sizeof(*state));
    if (state == NULL)
        return NULL;

    size_t elements = (size_t)MATRIX_N * MATRIX_N;
    state->a = malloc(elements * sizeof(double));
    state->b = malloc(elements * sizeof(double));
    state->c = malloc(elements * sizeof(double));
    if (state->a == NULL || state->b == NULL || state->c == NULL) {
        free(state->a);
        free(state->b);
        free(state->c);
        free(state);
        return NULL;
    }

    uint64_t seed = 0x9E3779B97F4A7C15ULL;
    for (size_t i = 0; i < elements; ++i) {
        state->a[i] = (double)(rng_next(&seed) >> 11) / 9007199254740992.0;
        state->b[i] = (double)(rng_next(&seed) >> 11) / 9007199254740992.0;
    }
    memset(state->c, 0, elements * sizeof(double));
    return state;
}

static void matrix_destroy(void *opaque)
{
    matrix_state *state = opaque;
    if (state == NULL)
        return;
    free(state->a);
    free(state->b);
    free(state->c);
    free(state);
}

static uint64_t matrix_run(void *opaque)
{
    matrix_state *state = opaque;
    const int n = MATRIX_N;

    for (int i = 0; i < n; ++i) {
        for (int j = 0; j < n; ++j) {
            double sum = 0.0;
            for (int k = 0; k < n; ++k)
                sum += state->a[(size_t)i * n + k] *
                       state->b[(size_t)k * n + j];
            state->c[(size_t)i * n + j] = sum;
        }
    }

    uint64_t hash = FNV_OFFSET;
    size_t elements = (size_t)n * n;
    for (size_t i = 0; i < elements; ++i) {
        uint64_t bits;
        memcpy(&bits, &state->c[i], sizeof(bits));
        hash = fnv_u64(hash, bits);
    }
    return hash;
}

/* --------------------------------------------------------------- SHA-256 */

typedef struct {
    uint32_t state[8];
    uint64_t bitlen;
    uint8_t data[64];
    size_t datalen;
} sha256_ctx;

static const uint32_t sha256_k[64] = {
    0x428a2f98, 0x71374491, 0xb5c0fbcf, 0xe9b5dba5,
    0x3956c25b, 0x59f111f1, 0x923f82a4, 0xab1c5ed5,
    0xd807aa98, 0x12835b01, 0x243185be, 0x550c7dc3,
    0x72be5d74, 0x80deb1fe, 0x9bdc06a7, 0xc19bf174,
    0xe49b69c1, 0xefbe4786, 0x0fc19dc6, 0x240ca1cc,
    0x2de92c6f, 0x4a7484aa, 0x5cb0a9dc, 0x76f988da,
    0x983e5152, 0xa831c66d, 0xb00327c8, 0xbf597fc7,
    0xc6e00bf3, 0xd5a79147, 0x06ca6351, 0x14292967,
    0x27b70a85, 0x2e1b2138, 0x4d2c6dfc, 0x53380d13,
    0x650a7354, 0x766a0abb, 0x81c2c92e, 0x92722c85,
    0xa2bfe8a1, 0xa81a664b, 0xc24b8b70, 0xc76c51a3,
    0xd192e819, 0xd6990624, 0xf40e3585, 0x106aa070,
    0x19a4c116, 0x1e376c08, 0x2748774c, 0x34b0bcb5,
    0x391c0cb3, 0x4ed8aa4a, 0x5b9cca4f, 0x682e6ff3,
    0x748f82ee, 0x78a5636f, 0x84c87814, 0x8cc70208,
    0x90befffa, 0xa4506ceb, 0xbef9a3f7, 0xc67178f2
};

static uint32_t rotr32(uint32_t value, uint32_t amount)
{
    return (value >> amount) | (value << (32 - amount));
}

static void sha256_transform(sha256_ctx *ctx, const uint8_t data[64])
{
    uint32_t m[64];

    for (uint32_t i = 0, j = 0; i < 16; ++i, j += 4) {
        m[i] = ((uint32_t)data[j] << 24) |
               ((uint32_t)data[j + 1] << 16) |
               ((uint32_t)data[j + 2] << 8) |
               (uint32_t)data[j + 3];
    }
    for (uint32_t i = 16; i < 64; ++i) {
        uint32_t s0 = rotr32(m[i - 15], 7) ^ rotr32(m[i - 15], 18) ^
                      (m[i - 15] >> 3);
        uint32_t s1 = rotr32(m[i - 2], 17) ^ rotr32(m[i - 2], 19) ^
                      (m[i - 2] >> 10);
        m[i] = m[i - 16] + s0 + m[i - 7] + s1;
    }

    uint32_t a = ctx->state[0];
    uint32_t b = ctx->state[1];
    uint32_t c = ctx->state[2];
    uint32_t d = ctx->state[3];
    uint32_t e = ctx->state[4];
    uint32_t f = ctx->state[5];
    uint32_t g = ctx->state[6];
    uint32_t h = ctx->state[7];

    for (uint32_t i = 0; i < 64; ++i) {
        uint32_t s1 = rotr32(e, 6) ^ rotr32(e, 11) ^ rotr32(e, 25);
        uint32_t ch = (e & f) ^ ((~e) & g);
        uint32_t temp1 = h + s1 + ch + sha256_k[i] + m[i];
        uint32_t s0 = rotr32(a, 2) ^ rotr32(a, 13) ^ rotr32(a, 22);
        uint32_t maj = (a & b) ^ (a & c) ^ (b & c);
        uint32_t temp2 = s0 + maj;

        h = g;
        g = f;
        f = e;
        e = d + temp1;
        d = c;
        c = b;
        b = a;
        a = temp1 + temp2;
    }

    ctx->state[0] += a;
    ctx->state[1] += b;
    ctx->state[2] += c;
    ctx->state[3] += d;
    ctx->state[4] += e;
    ctx->state[5] += f;
    ctx->state[6] += g;
    ctx->state[7] += h;
}

static void sha256_init(sha256_ctx *ctx)
{
    ctx->datalen = 0;
    ctx->bitlen = 0;
    ctx->state[0] = 0x6a09e667;
    ctx->state[1] = 0xbb67ae85;
    ctx->state[2] = 0x3c6ef372;
    ctx->state[3] = 0xa54ff53a;
    ctx->state[4] = 0x510e527f;
    ctx->state[5] = 0x9b05688c;
    ctx->state[6] = 0x1f83d9ab;
    ctx->state[7] = 0x5be0cd19;
}

static void sha256_update(sha256_ctx *ctx, const uint8_t *data, size_t len)
{
    while (len > 0) {
        size_t space = 64 - ctx->datalen;
        size_t take = len < space ? len : space;
        memcpy(ctx->data + ctx->datalen, data, take);
        ctx->datalen += take;
        data += take;
        len -= take;

        if (ctx->datalen == 64) {
            sha256_transform(ctx, ctx->data);
            ctx->bitlen += 512;
            ctx->datalen = 0;
        }
    }
}

static void sha256_final(sha256_ctx *ctx, uint8_t digest[32])
{
    uint64_t bitlen = ctx->bitlen + (uint64_t)ctx->datalen * 8;

    ctx->data[ctx->datalen++] = 0x80;
    if (ctx->datalen > 56) {
        while (ctx->datalen < 64)
            ctx->data[ctx->datalen++] = 0;
        sha256_transform(ctx, ctx->data);
        ctx->datalen = 0;
    }
    while (ctx->datalen < 56)
        ctx->data[ctx->datalen++] = 0;

    for (int i = 7; i >= 0; --i)
        ctx->data[ctx->datalen++] = (uint8_t)(bitlen >> (i * 8));

    sha256_transform(ctx, ctx->data);

    for (int i = 0; i < 8; ++i) {
        digest[i * 4] = (uint8_t)(ctx->state[i] >> 24);
        digest[i * 4 + 1] = (uint8_t)(ctx->state[i] >> 16);
        digest[i * 4 + 2] = (uint8_t)(ctx->state[i] >> 8);
        digest[i * 4 + 3] = (uint8_t)ctx->state[i];
    }
}

/* ---------------------------------------------------------------- hashing */

#define HASH_BUFFER_SIZE (1024u * 1024u)
#define HASH_REPEATS     64

typedef struct {
    uint8_t *buffer;
} hash_state;

static void *hash_create(void)
{
    hash_state *state = malloc(sizeof(*state));
    if (state == NULL)
        return NULL;

    state->buffer = malloc(HASH_BUFFER_SIZE);
    if (state->buffer == NULL) {
        free(state);
        return NULL;
    }

    uint64_t seed = 0x9E3779B97F4A7C15ULL;
    for (size_t i = 0; i < HASH_BUFFER_SIZE; ++i)
        state->buffer[i] = (uint8_t)(rng_next(&seed) & 0xff);
    return state;
}

static void hash_destroy(void *opaque)
{
    hash_state *state = opaque;
    if (state == NULL)
        return;
    free(state->buffer);
    free(state);
}

static uint64_t hash_run(void *opaque)
{
    hash_state *state = opaque;
    uint64_t hash = FNV_OFFSET;
    uint8_t digest[32];

    for (int repeat = 0; repeat < HASH_REPEATS; ++repeat) {
        sha256_ctx ctx;
        sha256_init(&ctx);
        sha256_update(&ctx, state->buffer, HASH_BUFFER_SIZE);
        sha256_final(&ctx, digest);
        for (int i = 0; i < 32; ++i) {
            hash ^= digest[i];
            hash *= FNV_PRIME;
        }
    }
    return hash;
}

/* --------------------------------------------------------------- registry */

const bench_workload bench_workloads[BENCH_WORKLOAD_COUNT] = {
    {
        "integer", "Integer", "numbers/s", SIEVE_LIMIT,
        0xa9562e85e580f34fULL,
        sieve_create, sieve_destroy, sieve_run
    },
    {
        "float", "Float", "pixels/s",
        (uint64_t)MB_WIDTH * MB_HEIGHT,
        0x7ed22ede6e983228ULL,
        mandel_create, mandel_destroy, mandel_run
    },
    {
        "memory", "Memory", "FLOP/s",
        2ULL * MATRIX_N * MATRIX_N * MATRIX_N,
        0x984d9fcfdce41067ULL,
        matrix_create, matrix_destroy, matrix_run
    },
    {
        "hashing", "Hashing", "bytes/s",
        (uint64_t)HASH_BUFFER_SIZE * HASH_REPEATS,
        0x758908d95f97d683ULL,
        hash_create, hash_destroy, hash_run
    }
};
