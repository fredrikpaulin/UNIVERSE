/*
 * generate.h — Procedural galaxy generation
 *
 * Given a seed and coordinates, deterministically generate
 * star systems with planets, resources, and orbital parameters.
 */
#ifndef GENERATE_H
#define GENERATE_H

#include "universe.h"
#include "rng.h"

/* Generate a full star system at the given galactic position.
 * The RNG should already be derived for this system's unique seed.
 * Fills in all fields of the system_t struct. */
void generate_system(system_t *sys, rng_t *rng, vec3_t galactic_pos);

/* Generate all systems in a sector. Returns number of systems generated.
 * Systems are written into the output array (caller provides storage).
 * max_systems is the capacity of the output array. */
int generate_sector(system_t *out, int max_systems,
                    uint64_t galaxy_seed, sector_coord_t coord);

/* How many stars should a sector at this galactic position contain?
 * More stars near spiral arms and galactic core, fewer in the halo. */
int sector_star_count(rng_t *rng, sector_coord_t coord);

/* Calculate habitable zone boundaries for a star.
 * Returns inner and outer radius in AU. */
void habitable_zone(double luminosity_solar, double *inner_au, double *outer_au);

/* Generate a UID from an RNG (two 64-bit draws) */
probe_uid_t generate_uid(rng_t *rng);

#endif
