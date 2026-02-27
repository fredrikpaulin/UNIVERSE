/*
 * rng.c — xoshiro256** PRNG implementation
 *
 * Reference: https://prng.di.unimi.it/xoshiro256starstar.c
 * Public domain by Sebastiano Vigna and David Blackman.
 */
#define _USE_MATH_DEFINES
#include "rng.h"
#include <math.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* splitmix64 — used to seed xoshiro from a single uint64 */
static uint64_t splitmix64(uint64_t *state) {
    uint64_t z = (*state += 0x9e3779b97f4a7c15ULL);
    z = (z ^ (z >> 30)) * 0xbf58476d1ce4e5b9ULL;
    z = (z ^ (z >> 27)) * 0x94d049bb133111ebULL;
    return z ^ (z >> 31);
}

void rng_seed(rng_t *rng, uint64_t seed) {
    uint64_t sm = seed;
    rng->s[0] = splitmix64(&sm);
    rng->s[1] = splitmix64(&sm);
    rng->s[2] = splitmix64(&sm);
    rng->s[3] = splitmix64(&sm);
}

static inline uint64_t rotl(uint64_t x, int k) {
    return (x << k) | (x >> (64 - k));
}

uint64_t rng_next(rng_t *rng) {
    uint64_t *s = rng->s;
    uint64_t result = rotl(s[1] * 5, 7) * 9;
    uint64_t t = s[1] << 17;

    s[2] ^= s[0];
    s[3] ^= s[1];
    s[1] ^= s[2];
    s[0] ^= s[3];
    s[2] ^= t;
    s[3] = rotl(s[3], 45);

    return result;
}

double rng_double(rng_t *rng) {
    return (rng_next(rng) >> 11) * 0x1.0p-53;
}

uint64_t rng_range(rng_t *rng, uint64_t max) {
    if (max == 0) return 0;
    /* Unbiased rejection sampling */
    uint64_t threshold = (-max) % max;
    for (;;) {
        uint64_t r = rng_next(rng);
        if (r >= threshold)
            return r % max;
    }
}

double rng_gaussian(rng_t *rng) {
    /* Box-Muller transform */
    double u1 = rng_double(rng);
    double u2 = rng_double(rng);
    /* Avoid log(0) */
    while (u1 == 0.0) u1 = rng_double(rng);
    return sqrt(-2.0 * log(u1)) * cos(2.0 * M_PI * u2);
}

void rng_derive(rng_t *rng, uint64_t seed, int32_t x, int32_t y, int32_t z) {
    /* Mix coordinates into the seed deterministically */
    uint64_t combined = seed;
    combined ^= (uint64_t)(uint32_t)x * 0x517cc1b727220a95ULL;
    combined ^= (uint64_t)(uint32_t)y * 0x6c62272e07bb0142ULL;
    combined ^= (uint64_t)(uint32_t)z * 0x9e3779b97f4a7c15ULL;
    rng_seed(rng, combined);
}
