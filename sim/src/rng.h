/*
 * rng.h — Seeded PRNG using xoshiro256**
 *
 * Deterministic, fast, high-quality. Given the same seed,
 * always produces the same sequence on any platform.
 */
#ifndef RNG_H
#define RNG_H

#include <stdint.h>

typedef struct {
    uint64_t s[4];
} rng_t;

/* Seed from a single 64-bit value (uses splitmix64 to fill state) */
void     rng_seed(rng_t *rng, uint64_t seed);

/* Next random uint64 */
uint64_t rng_next(rng_t *rng);

/* Uniform double in [0, 1) */
double   rng_double(rng_t *rng);

/* Uniform int in [0, max) */
uint64_t rng_range(rng_t *rng, uint64_t max);

/* Gaussian (normal) with mean 0 and stddev 1 */
double   rng_gaussian(rng_t *rng);

/* Derive a new RNG from a parent seed + extra data (for sector generation etc.) */
void     rng_derive(rng_t *rng, uint64_t seed, int32_t x, int32_t y, int32_t z);

#endif
