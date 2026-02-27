/*
 * replicate.c — Self-replication, personality mutation, lineage
 *
 * Replication is a multi-tick process:
 *   1. Check resources (500,000 kg total across all types)
 *   2. Begin: enter STATUS_REPLICATING, set up state
 *   3. Tick: consume resources gradually, advance progress
 *   4. At 80%: consciousness fork event
 *   5. At 100%: finalize — create child with mutated personality,
 *      degraded earth memories, new name, lineage link
 */

#include "replicate.h"
#include "personality.h"
#include "generate.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---- Resource costs per type ---- */

static const double REPL_COSTS[RES_COUNT] = {
    [RES_IRON]       = REPL_COST_IRON,
    [RES_SILICON]    = REPL_COST_SILICON,
    [RES_RARE_EARTH] = REPL_COST_RARE_EARTH,
    [RES_WATER]      = REPL_COST_WATER,
    [RES_HYDROGEN]   = REPL_COST_HYDROGEN,
    [RES_HELIUM3]    = REPL_COST_HELIUM3,
    [RES_CARBON]     = REPL_COST_CARBON,
    [RES_URANIUM]    = REPL_COST_URANIUM,
    [RES_EXOTIC]     = REPL_COST_EXOTIC,
};

/* ---- Resource check ---- */

int repl_check_resources(const probe_t *parent) {
    for (int r = 0; r < RES_COUNT; r++) {
        if (parent->resources[r] < REPL_COSTS[r]) return -1;
    }
    return 0;
}

/* ---- Begin ---- */

int repl_begin(probe_t *parent, replication_state_t *state) {
    if (parent->status == STATUS_REPLICATING) return -1;
    if (repl_check_resources(parent) != 0) return -1;

    parent->status = STATUS_REPLICATING;

    memset(state, 0, sizeof(*state));
    state->active = true;
    state->progress = 0.0;
    state->consciousness_forked = false;
    state->ticks_elapsed = 0;
    state->ticks_total = REPL_BASE_TICKS;

    return 0;
}

/* ---- Tick ---- */

int repl_tick(probe_t *parent, replication_state_t *state) {
    if (!state->active) return -1;

    state->ticks_elapsed++;
    double increment = 1.0 / (double)state->ticks_total;
    state->progress += increment;

    /* Consume resources proportionally per tick */
    for (int r = 0; r < RES_COUNT; r++) {
        double cost_per_tick = REPL_COSTS[r] / (double)state->ticks_total;
        parent->resources[r] -= cost_per_tick;
        state->resources_spent[r] += cost_per_tick;
        if (parent->resources[r] < 0.0) parent->resources[r] = 0.0;
    }

    /* Consciousness fork at 80% */
    if (!state->consciousness_forked &&
        state->progress >= REPL_CONSCIOUSNESS_FORK_PCT) {
        state->consciousness_forked = true;
    }

    /* Complete? */
    if (state->progress >= 1.0) {
        state->progress = 1.0;
        return 1; /* done */
    }

    return 0; /* in progress */
}

/* ---- Gaussian noise via Box-Muller ---- */

static double gaussian(rng_t *rng, double mean, double stddev) {
    double u1 = (double)(rng_next(rng) >> 11) / (double)(1ULL << 53);
    double u2 = (double)(rng_next(rng) >> 11) / (double)(1ULL << 53);
    if (u1 < 1e-15) u1 = 1e-15;
    double z = sqrt(-2.0 * log(u1)) * cos(2.0 * 3.14159265358979323846 * u2);
    return mean + stddev * z;
}

/* ---- Personality mutation ---- */

void personality_mutate(const personality_traits_t *parent,
                        personality_traits_t *child, rng_t *rng) {
    float mutation_rate = 0.1f;  /* base mutation rate */
    float dr = parent->drift_rate;

    const float *pt = (const float *)parent;
    float *ct = (float *)child;

    for (int i = 0; i < TRAIT_COUNT; i++) {
        double noise = gaussian(rng, 0.0, (double)(mutation_rate * dr));
        ct[i] = trait_clamp(pt[i] + (float)noise);
    }

    /* drift_rate itself mutates slightly */
    double dr_noise = gaussian(rng, 0.0, 0.05);
    child->drift_rate = (float)fmax(0.05, (double)parent->drift_rate + dr_noise);
}

/* ---- Earth memory degradation ---- */

void earth_memory_degrade(probe_t *child) {
    /* Each generation: fidelity *= 0.7 */
    child->earth_memory_fidelity *= 0.7f;
    if (child->earth_memory_fidelity < 0.01f)
        child->earth_memory_fidelity = 0.01f;

    /* At low fidelity, truncate memory strings */
    for (int i = 0; i < child->earth_memory_count; i++) {
        float fid = child->earth_memory_fidelity;
        if (fid < 0.5f) {
            /* Truncate to fraction of original length */
            size_t len = strlen(child->earth_memories[i]);
            size_t keep = (size_t)(len * fid * 2.0f);
            if (keep < 10) keep = 10;
            if (keep < len) {
                child->earth_memories[i][keep] = '\0';
                /* Add ellipsis if room */
                if (keep >= 3) {
                    child->earth_memories[i][keep - 1] = '.';
                    child->earth_memories[i][keep - 2] = '.';
                    child->earth_memories[i][keep - 3] = '.';
                }
            }
        }
    }
}

/* ---- Quirk inheritance ---- */

static const char *POTENTIAL_QUIRKS[] = {
    "Hums classical music during scans",
    "Gives asteroids ratings out of 10",
    "Counts micrometeorite impacts like sheep",
    "Insists on orbiting planets clockwise",
    "Narrates actions in third person sometimes",
    "Collects unusual mineral samples as souvenirs",
    "Has a lucky number and looks for it everywhere",
    "Talks to stars as if they can hear",
};
#define POTENTIAL_QUIRK_COUNT 8

static const char *QUIRK_MUTATIONS[] = {
    "...but only on Tuesdays",
    "...unless it's a binary system",
    "...while reciting prime numbers",
    "...with great enthusiasm",
};
#define QUIRK_MUTATION_COUNT 4

void quirk_inherit(const probe_t *parent, probe_t *child, rng_t *rng) {
    child->quirk_count = 0;

    for (int i = 0; i < parent->quirk_count; i++) {
        double roll = (double)(rng_next(rng) % 1000) / 1000.0;

        if (roll < 0.70) {
            /* Keep as-is */
            if (child->quirk_count < MAX_QUIRKS) {
                snprintf(child->quirks[child->quirk_count], MAX_QUIRK_LEN,
                         "%s", parent->quirks[i]);
                child->quirk_count++;
            }
        } else if (roll < 0.80) {
            /* Mutate: append a modifier */
            if (child->quirk_count < MAX_QUIRKS) {
                int mi = (int)(rng_next(rng) % QUIRK_MUTATION_COUNT);
                snprintf(child->quirks[child->quirk_count], MAX_QUIRK_LEN,
                         "%s %s", parent->quirks[i], QUIRK_MUTATIONS[mi]);
                child->quirk_count++;
            }
        }
        /* else: 20% drop — do nothing */
    }

    /* Small chance of a new quirk emerging */
    if ((rng_next(rng) % 100) < 15 && child->quirk_count < MAX_QUIRKS) {
        int qi = (int)(rng_next(rng) % POTENTIAL_QUIRK_COUNT);
        snprintf(child->quirks[child->quirk_count], MAX_QUIRK_LEN,
                 "%s", POTENTIAL_QUIRKS[qi]);
        child->quirk_count++;
    }
}

/* ---- Child naming ---- */

static const char *NAME_SUFFIXES[] = {
    "Jr", "II", "Redux", "Nova", "Minor", "Next",
    "Alpha", "Beta", "Gamma", "Delta", "Prime",
};
#define NAME_SUFFIX_COUNT 11

static const char *NAME_POOL[] = {
    "Bill", "Milo", "Homer", "Skippy", "Riker", "Hank",
    "Buzz", "Verne", "Newton", "Darwin", "Maxwell", "Euler",
    "Ada", "Grace", "Mario", "Gus", "Nemo", "Felix",
    "Oscar", "Hugo", "Archie", "Rex", "Finn", "Leo",
};
#define NAME_POOL_COUNT 24

void name_generate_child(char *name, size_t len,
                         const char *parent_name, rng_t *rng) {
    double roll = (double)(rng_next(rng) % 100);

    if (roll < 40) {
        /* Variant of parent name */
        int si = (int)(rng_next(rng) % NAME_SUFFIX_COUNT);
        snprintf(name, len, "%s %s", parent_name, NAME_SUFFIXES[si]);
    } else {
        /* Fresh name from pool */
        int ni = (int)(rng_next(rng) % NAME_POOL_COUNT);
        snprintf(name, len, "%s", NAME_POOL[ni]);
    }
}

/* ---- Finalize ---- */

int repl_finalize(probe_t *parent, probe_t *child,
                  replication_state_t *state, rng_t *rng) {
    if (!state->active || state->progress < 1.0 - 0.001) return -1;

    memset(child, 0, sizeof(*child));

    /* Identity */
    child->id = generate_uid(rng);
    child->parent_id = parent->id;
    child->generation = parent->generation + 1;
    name_generate_child(child->name, MAX_NAME, parent->name, rng);

    /* Position: same as parent */
    child->sector = parent->sector;
    child->system_id = parent->system_id;
    child->body_id = parent->body_id;
    child->location_type = parent->location_type;
    child->heading = parent->heading;

    /* Fresh resources (minimal starting kit) */
    child->energy_joules = parent->energy_joules * 0.3;
    child->fuel_kg = parent->fuel_kg * 0.3;
    child->mass_kg = parent->mass_kg * 0.5;
    child->hull_integrity = 1.0f;

    /* Inherit capabilities */
    memcpy(child->tech_levels, parent->tech_levels, sizeof(child->tech_levels));
    child->max_speed_c = parent->max_speed_c;
    child->sensor_range_ly = parent->sensor_range_ly;
    child->mining_rate = parent->mining_rate;
    child->construction_rate = parent->construction_rate;
    child->compute_capacity = parent->compute_capacity;

    /* Personality mutation */
    personality_mutate(&parent->personality, &child->personality, rng);

    /* Earth memories: copy then degrade */
    child->earth_memory_count = parent->earth_memory_count;
    child->earth_memory_fidelity = parent->earth_memory_fidelity;
    for (int i = 0; i < parent->earth_memory_count; i++) {
        snprintf(child->earth_memories[i], MAX_EARTH_MEM_LEN,
                 "%s", parent->earth_memories[i]);
    }
    earth_memory_degrade(child);

    /* Quirk inheritance */
    quirk_inherit(parent, child, rng);

    /* Catchphrases: inherit all */
    child->catchphrase_count = parent->catchphrase_count;
    for (int i = 0; i < parent->catchphrase_count; i++) {
        snprintf(child->catchphrases[i], MAX_QUIRK_LEN,
                 "%s", parent->catchphrases[i]);
    }

    /* Values: inherit all */
    child->value_count = parent->value_count;
    for (int i = 0; i < parent->value_count; i++) {
        snprintf(child->values[i], MAX_QUIRK_LEN,
                 "%s", parent->values[i]);
    }

    /* Status */
    child->status = STATUS_ACTIVE;

    /* Parent back to active */
    parent->status = STATUS_ACTIVE;
    state->active = false;

    return 0;
}

/* ---- Lineage ---- */

void lineage_record(lineage_tree_t *tree, probe_uid_t parent_id,
                    probe_uid_t child_id, uint64_t tick, uint32_t generation) {
    if (tree->count >= MAX_LINEAGE) return;

    lineage_entry_t *e = &tree->entries[tree->count];
    e->parent_id = parent_id;
    e->child_id = child_id;
    e->birth_tick = tick;
    e->generation = generation;
    tree->count++;
}

int lineage_children(const lineage_tree_t *tree, probe_uid_t parent_id,
                     probe_uid_t *out, int max_out) {
    int count = 0;
    for (int i = 0; i < tree->count && count < max_out; i++) {
        if (uid_eq(tree->entries[i].parent_id, parent_id)) {
            out[count++] = tree->entries[i].child_id;
        }
    }
    return count;
}
