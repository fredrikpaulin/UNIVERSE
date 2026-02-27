/*
 * test_replicate.c — Phase 7: Self-replication, mutation, lineage
 *
 * Tests:
 *   - Resource check (sufficient/insufficient)
 *   - Begin replication → status change
 *   - Replication tick progress
 *   - Replication complete after N ticks
 *   - Child has mutated personality
 *   - Child has degraded earth memories
 *   - Child has a different name
 *   - Lineage tree records parent→child
 *   - Both probes independent
 *   - Insufficient resources rejected
 *   - Interrupted replication resumable
 *   - Quirk inheritance
 *   - Name generation
 */

#include "universe.h"
#include "replicate.h"
#include "personality.h"
#include "probe.h"
#include "generate.h"
#include "rng.h"
#include "util.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

static int g_pass = 0, g_fail = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { g_pass++; } \
    else { g_fail++; fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__); } \
} while(0)

#define ASSERT_NEAR(a, b, eps, msg) ASSERT(fabs((double)(a) - (double)(b)) < (eps), msg)

/* Helper: give probe enough resources for replication */
static void give_repl_resources(probe_t *p) {
    p->resources[RES_IRON]       = REPL_COST_IRON + 1000;
    p->resources[RES_SILICON]    = REPL_COST_SILICON + 1000;
    p->resources[RES_RARE_EARTH] = REPL_COST_RARE_EARTH + 1000;
    p->resources[RES_CARBON]     = REPL_COST_CARBON + 1000;
    p->resources[RES_WATER]      = REPL_COST_WATER + 1000;
    p->resources[RES_URANIUM]    = REPL_COST_URANIUM + 1000;
    p->resources[RES_HYDROGEN]   = REPL_COST_HYDROGEN + 1000;
    p->resources[RES_HELIUM3]    = REPL_COST_HELIUM3 + 1000;
    p->resources[RES_EXOTIC]     = REPL_COST_EXOTIC + 1000;
    p->energy_joules = 1e12;
}

/* ---- Test: Resource check ---- */

static void test_resource_check(void) {
    printf("Test: Resource check for replication\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);

    /* Bob starts with no resources → should fail */
    ASSERT(repl_check_resources(&probe) == -1, "No resources → rejected");

    /* Give enough */
    give_repl_resources(&probe);
    ASSERT(repl_check_resources(&probe) == 0, "Sufficient resources → ready");

    /* Remove one resource */
    probe.resources[RES_EXOTIC] = 0.0;
    ASSERT(repl_check_resources(&probe) == -1, "Missing exotic → rejected");
}

/* ---- Test: Begin replication ---- */

static void test_begin_replication(void) {
    printf("Test: Begin replication\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);
    give_repl_resources(&probe);
    probe.status = STATUS_ACTIVE;

    replication_state_t state;
    memset(&state, 0, sizeof(state));

    int rc = repl_begin(&probe, &state);
    ASSERT(rc == 0, "Replication started successfully");
    ASSERT(probe.status == STATUS_REPLICATING, "Status → REPLICATING");
    ASSERT(state.active, "State is active");
    ASSERT_NEAR(state.progress, 0.0, 0.01, "Progress starts at 0");
    ASSERT(state.ticks_total > 0, "Total ticks set");

    /* Can't begin if already replicating */
    rc = repl_begin(&probe, &state);
    ASSERT(rc == -1, "Can't start while already replicating");

    /* Can't begin without resources */
    probe_t poor;
    memset(&poor, 0, sizeof(poor));
    probe_init_bob(&poor);
    poor.status = STATUS_ACTIVE;
    replication_state_t state2;
    memset(&state2, 0, sizeof(state2));
    rc = repl_begin(&poor, &state2);
    ASSERT(rc == -1, "No resources → can't begin");
}

/* ---- Test: Replication tick progress ---- */

static void test_replication_ticks(void) {
    printf("Test: Replication tick progress\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);
    give_repl_resources(&probe);
    probe.status = STATUS_ACTIVE;

    replication_state_t state;
    memset(&state, 0, sizeof(state));
    repl_begin(&probe, &state);

    double initial_iron = probe.resources[RES_IRON];

    /* Tick a few times */
    for (int i = 0; i < 10; i++) {
        int rc = repl_tick(&probe, &state);
        ASSERT(rc == 0, "Tick in progress");
    }
    ASSERT(state.progress > 0.0, "Progress advanced");
    ASSERT(state.ticks_elapsed == 10, "10 ticks elapsed");
    ASSERT(probe.resources[RES_IRON] < initial_iron, "Iron consumed");

    /* Consciousness fork at 80% */
    bool forked = false;
    while (state.progress < 1.0) {
        int rc = repl_tick(&probe, &state);
        if (state.consciousness_forked && !forked) {
            forked = true;
            ASSERT(state.progress >= REPL_CONSCIOUSNESS_FORK_PCT - 0.01,
                   "Fork at ~80%");
        }
        if (rc == 1) break;
    }
    ASSERT(forked, "Consciousness fork happened");
    ASSERT(state.progress >= 1.0 - 0.001, "Reached 100%");
}

/* ---- Test: Full replication cycle ---- */

static void test_full_replication(void) {
    printf("Test: Full replication cycle → child probe\n");

    probe_t parent;
    memset(&parent, 0, sizeof(parent));
    probe_init_bob(&parent);
    give_repl_resources(&parent);
    parent.status = STATUS_ACTIVE;
    parent.created_tick = 0;

    replication_state_t state;
    memset(&state, 0, sizeof(state));
    repl_begin(&parent, &state);

    /* Run to completion */
    while (repl_tick(&parent, &state) == 0) {}

    /* Finalize */
    rng_t rng;
    rng_seed(&rng, 12345);

    probe_t child;
    memset(&child, 0, sizeof(child));
    int rc = repl_finalize(&parent, &child, &state, &rng);
    ASSERT(rc == 0, "Finalize succeeded");

    /* Child exists */
    ASSERT(child.generation == parent.generation + 1,
           "Child generation = parent + 1");
    ASSERT(uid_eq(child.parent_id, parent.id),
           "Child's parent_id = parent's id");
    ASSERT(!uid_is_null(child.id), "Child has an ID");
    ASSERT(!uid_eq(child.id, parent.id), "Child ID != parent ID");

    /* Parent back to active */
    ASSERT(parent.status == STATUS_ACTIVE, "Parent back to ACTIVE");
}

/* ---- Test: Personality mutation ---- */

static void test_personality_mutation(void) {
    printf("Test: Child has mutated personality\n");

    probe_t parent;
    memset(&parent, 0, sizeof(parent));
    probe_init_bob(&parent);
    give_repl_resources(&parent);
    parent.status = STATUS_ACTIVE;

    replication_state_t state;
    memset(&state, 0, sizeof(state));
    repl_begin(&parent, &state);
    while (repl_tick(&parent, &state) == 0) {}

    rng_t rng;
    rng_seed(&rng, 42);

    probe_t child;
    memset(&child, 0, sizeof(child));
    repl_finalize(&parent, &child, &state, &rng);

    /* Personality should differ but be within range */
    bool any_different = false;
    float *pt = (float *)&parent.personality;
    float *ct = (float *)&child.personality;
    for (int i = 0; i < TRAIT_COUNT; i++) {
        if (fabsf(pt[i] - ct[i]) > 0.001f) any_different = true;
        ASSERT(ct[i] >= -1.0f && ct[i] <= 1.0f,
               "Child trait in [-1, 1]");
    }
    ASSERT(any_different, "At least one trait differs from parent");

    /* drift_rate itself can mutate */
    /* (just check it's valid) */
    ASSERT(child.personality.drift_rate > 0.0f,
           "Child drift_rate > 0");
}

/* ---- Test: Earth memory degradation ---- */

static void test_earth_memory_degradation(void) {
    printf("Test: Child has degraded earth memories\n");

    probe_t parent;
    memset(&parent, 0, sizeof(parent));
    probe_init_bob(&parent);
    give_repl_resources(&parent);
    parent.status = STATUS_ACTIVE;

    replication_state_t state;
    memset(&state, 0, sizeof(state));
    repl_begin(&parent, &state);
    while (repl_tick(&parent, &state) == 0) {}

    rng_t rng;
    rng_seed(&rng, 99);

    probe_t child;
    memset(&child, 0, sizeof(child));
    repl_finalize(&parent, &child, &state, &rng);

    /* Child should have lower fidelity */
    ASSERT(child.earth_memory_fidelity < parent.earth_memory_fidelity,
           "Child fidelity < parent fidelity");
    ASSERT(child.earth_memory_fidelity > 0.0f,
           "Gen 1 still has some fidelity");

    /* Child still has earth memories */
    ASSERT(child.earth_memory_count > 0,
           "Child inherited earth memories");

    /* Multi-generation degradation */
    probe_t gen2 = child;
    gen2.generation = 2;
    earth_memory_degrade(&gen2);
    ASSERT(gen2.earth_memory_fidelity < child.earth_memory_fidelity,
           "Gen 2 fidelity < gen 1");

    /* By gen 5+, fidelity should be very low */
    probe_t gen5 = child;
    gen5.generation = 5;
    for (int g = 1; g < 5; g++) {
        earth_memory_degrade(&gen5);
    }
    ASSERT(gen5.earth_memory_fidelity < 0.3f,
           "Gen 5+ fidelity very low");
}

/* ---- Test: Child naming ---- */

static void test_child_naming(void) {
    printf("Test: Child has a unique name\n");

    rng_t rng;
    rng_seed(&rng, 77);

    char name1[MAX_NAME], name2[MAX_NAME];
    name_generate_child(name1, MAX_NAME, "Bob", &rng);
    name_generate_child(name2, MAX_NAME, "Bob", &rng);

    ASSERT(strlen(name1) > 0, "Name 1 non-empty");
    ASSERT(strlen(name2) > 0, "Name 2 non-empty");
    ASSERT(strcmp(name1, "Bob") != 0, "Name 1 != parent");
    ASSERT(strcmp(name1, name2) != 0, "Two children get different names");
}

/* ---- Test: Lineage tree ---- */

static void test_lineage_tree(void) {
    printf("Test: Lineage tree\n");

    lineage_tree_t tree;
    memset(&tree, 0, sizeof(tree));

    probe_uid_t bob_id  = { 0, 1 };
    probe_uid_t kid1_id = { 0, 2 };
    probe_uid_t kid2_id = { 0, 3 };

    lineage_record(&tree, bob_id, kid1_id, 1000, 1);
    ASSERT(tree.count == 1, "1 entry");

    lineage_record(&tree, bob_id, kid2_id, 2000, 1);
    ASSERT(tree.count == 2, "2 entries");

    /* Lookup children */
    probe_uid_t children[8];
    int count = lineage_children(&tree, bob_id, children, 8);
    ASSERT(count == 2, "Bob has 2 children");
    ASSERT(uid_eq(children[0], kid1_id) || uid_eq(children[1], kid1_id),
           "Kid1 found");
    ASSERT(uid_eq(children[0], kid2_id) || uid_eq(children[1], kid2_id),
           "Kid2 found");

    /* Unknown parent → 0 children */
    probe_uid_t nobody = { 0, 99 };
    count = lineage_children(&tree, nobody, children, 8);
    ASSERT(count == 0, "Unknown parent has 0 children");
}

/* ---- Test: Quirk inheritance ---- */

static void test_quirk_inheritance(void) {
    printf("Test: Quirk inheritance\n");

    probe_t parent;
    memset(&parent, 0, sizeof(parent));
    probe_init_bob(&parent); /* 3 quirks */

    rng_t rng;
    rng_seed(&rng, 55);

    /* Run many trials to test statistical distribution */
    int kept_total = 0;
    int trials = 100;
    for (int t = 0; t < trials; t++) {
        probe_t child;
        memset(&child, 0, sizeof(child));
        quirk_inherit(&parent, &child, &rng);

        /* Child should have some quirks (0 to MAX_QUIRKS) */
        ASSERT(child.quirk_count <= MAX_QUIRKS, "Quirk count within bounds");
        kept_total += child.quirk_count;
    }

    /* On average, 70% of 3 quirks = 2.1 kept, plus some new ones.
     * Should be roughly 1-4 per child on average. */
    double avg = (double)kept_total / trials;
    ASSERT(avg > 0.5 && avg < 5.0,
           "Average quirk count reasonable");
}

/* ---- Test: Interrupted replication ---- */

static void test_interrupted_replication(void) {
    printf("Test: Interrupted replication is resumable\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);
    give_repl_resources(&probe);
    probe.status = STATUS_ACTIVE;

    replication_state_t state;
    memset(&state, 0, sizeof(state));
    repl_begin(&probe, &state);

    /* Tick partway */
    for (int i = 0; i < 50; i++) {
        repl_tick(&probe, &state);
    }
    double progress_at_interrupt = state.progress;
    ASSERT(progress_at_interrupt > 0.0, "Made some progress");
    ASSERT(progress_at_interrupt < 1.0, "Not yet complete");

    /* Simulate interruption: hull damage */
    probe.hull_integrity = 0.3f;
    probe.status = STATUS_DAMAGED;

    /* Later, resume */
    probe.hull_integrity = 0.9f;
    probe.status = STATUS_REPLICATING;

    /* Continue ticking from where we left off */
    int rc;
    do {
        rc = repl_tick(&probe, &state);
    } while (rc == 0);

    ASSERT(rc == 1, "Completed after resuming");
    ASSERT(state.progress >= 1.0 - 0.001, "Full progress after resume");
}

/* ---- Test: Both probes tick independently ---- */

static void test_independent_probes(void) {
    printf("Test: Parent and child tick independently\n");

    probe_t parent;
    memset(&parent, 0, sizeof(parent));
    probe_init_bob(&parent);
    give_repl_resources(&parent);
    parent.status = STATUS_ACTIVE;

    replication_state_t state;
    memset(&state, 0, sizeof(state));
    repl_begin(&parent, &state);
    while (repl_tick(&parent, &state) == 0) {}

    rng_t rng;
    rng_seed(&rng, 333);

    probe_t child;
    memset(&child, 0, sizeof(child));
    repl_finalize(&parent, &child, &state, &rng);

    /* Both should be active */
    ASSERT(parent.status == STATUS_ACTIVE, "Parent active");
    ASSERT(child.status == STATUS_ACTIVE, "Child active");

    /* Modify one, the other shouldn't change */
    parent.hull_integrity = 0.5f;
    ASSERT(child.hull_integrity >= 0.99f, "Child hull unaffected");

    child.personality.curiosity = -0.5f;
    ASSERT(parent.personality.curiosity > 0.0f,
           "Parent personality unaffected");
}

/* ---- Main ---- */

int main(void) {
    printf("=== Phase 7: Replication Tests ===\n\n");

    test_resource_check();
    test_begin_replication();
    test_replication_ticks();
    test_full_replication();
    test_personality_mutation();
    test_earth_memory_degradation();
    test_child_naming();
    test_lineage_tree();
    test_quirk_inheritance();
    test_interrupted_replication();
    test_independent_probes();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
