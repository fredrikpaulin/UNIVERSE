#ifndef REPLICATE_H
#define REPLICATE_H

#include "universe.h"
#include "rng.h"

/* ---- Replication cost ---- */

/* Total material cost in kg per resource type */
#define REPL_COST_IRON       200000.0
#define REPL_COST_SILICON     100000.0
#define REPL_COST_RARE_EARTH  50000.0
#define REPL_COST_CARBON      50000.0
#define REPL_COST_WATER       50000.0
#define REPL_COST_URANIUM     25000.0
#define REPL_COST_HYDROGEN    15000.0
#define REPL_COST_HELIUM3      5000.0
#define REPL_COST_EXOTIC       5000.0
#define REPL_TOTAL_KG        500000.0

/* Ticks to complete replication */
#define REPL_BASE_TICKS 200
#define REPL_CONSCIOUSNESS_FORK_PCT 0.80  /* fork at 80% */

/* ---- Replication state (embedded in probe or tracked externally) ---- */

typedef struct {
    bool     active;
    double   progress;            /* 0.0 to 1.0 */
    double   resources_spent[RES_COUNT];
    bool     consciousness_forked;
    uint32_t ticks_elapsed;
    uint32_t ticks_total;
} replication_state_t;

/* ---- API ---- */

/* Check if probe has enough resources to begin replication.
 * Returns 0 if ready, -1 if insufficient. */
int repl_check_resources(const probe_t *parent);

/* Begin replication. Sets probe status to STATUS_REPLICATING.
 * Returns 0 on success, -1 if resources insufficient or state invalid. */
int repl_begin(probe_t *parent, replication_state_t *state);

/* Advance replication by one tick. Consumes resources gradually.
 * Returns: 0 = in progress, 1 = complete, -1 = error */
int repl_tick(probe_t *parent, replication_state_t *state);

/* Finalize: create the child probe. Applies personality mutation,
 * earth memory degradation, name generation, lineage setup.
 * Writes child into *child. Returns 0 on success. */
int repl_finalize(probe_t *parent, probe_t *child,
                  replication_state_t *state, rng_t *rng);

/* ---- Personality mutation ---- */

/* Mutate parent personality into child with gaussian noise.
 * child_trait = parent_trait + gaussian(0, mutation_rate * drift_rate) */
void personality_mutate(const personality_traits_t *parent,
                        personality_traits_t *child, rng_t *rng);

/* ---- Earth memory degradation ---- */

/* Degrade earth memories for a child probe.
 * Fidelity drops per generation. Strings get truncated at low fidelity. */
void earth_memory_degrade(probe_t *child);

/* ---- Quirk inheritance ---- */

/* Inherit quirks from parent: 70% keep, 10% mutate, 20% drop.
 * May add a new random quirk. */
void quirk_inherit(const probe_t *parent, probe_t *child, rng_t *rng);

/* ---- Child naming ---- */

/* Generate a name for the child probe. Rule-based: parent name variant. */
void name_generate_child(char *name, size_t len,
                         const char *parent_name, rng_t *rng);

/* ---- Lineage ---- */

typedef struct {
    probe_uid_t parent_id;
    probe_uid_t child_id;
    uint64_t    birth_tick;
    uint32_t    generation;
} lineage_entry_t;

#define MAX_LINEAGE 1024

typedef struct {
    lineage_entry_t entries[MAX_LINEAGE];
    int count;
} lineage_tree_t;

/* Record a parent→child relationship */
void lineage_record(lineage_tree_t *tree, probe_uid_t parent_id,
                    probe_uid_t child_id, uint64_t tick, uint32_t generation);

/* Get children of a given probe. Returns count, writes up to max_out IDs. */
int lineage_children(const lineage_tree_t *tree, probe_uid_t parent_id,
                     probe_uid_t *out, int max_out);

#endif /* REPLICATE_H */
