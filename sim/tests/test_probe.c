#define _POSIX_C_SOURCE 199309L
/*
 * test_probe.c — Phase 2 tests: probe actions, resource model, energy, fuel
 *
 * Written BEFORE implementation. These tests define the probe.h API contract.
 * Implementation must make all these pass.
 *
 * Tests the spec requirements:
 *   - Bob starts at his origin system, fuel at initial level
 *   - Commanding Bob to orbit, survey, land, mine — each modifies state correctly
 *   - Mining 100 ticks on high-abundance planet yields more than low-abundance
 *   - Can't land on a gas giant (action rejected)
 *   - Can't mine while in transit (action rejected)
 *   - Fuel decreases during maneuvers, energy decreases during actions
 *   - Save/load probe state — round-trip produces identical probe
 */
#include "universe.h"
#include "rng.h"
#include "generate.h"
#include "persist.h"
#include "probe.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, tol, msg) ASSERT(fabs((double)(a)-(double)(b)) < (tol), msg)

/* ---- Helper: generate a system with known properties ---- */

/* Generate a test system with at least one rocky planet and one gas giant */
static void make_test_system(system_t *sys) {
    rng_t rng;
    rng_seed(&rng, 42);
    /* Generate sector (0,0,0) and pick the first system */
    system_t systems[30];
    int count = generate_sector(systems, 30, 42, (sector_coord_t){0, 0, 0});
    (void)count;
    *sys = systems[0];
}

/* ---- Test: Bob initialization ---- */
static void test_bob_init(void) {
    printf("Test: Bob initialization\n");

    probe_t bob;
    int rc = probe_init_bob(&bob);
    ASSERT(rc == 0, "probe_init_bob succeeds");
    ASSERT(strcmp(bob.name, "Bob") == 0, "Name is Bob");
    ASSERT(bob.generation == 0, "Generation 0");
    ASSERT(uid_is_null(bob.parent_id), "No parent");
    ASSERT(bob.fuel_kg > 0, "Has fuel");
    ASSERT(bob.energy_joules > 0, "Has energy");
    ASSERT(bob.hull_integrity == 1.0f, "Full hull");
    ASSERT(bob.status == STATUS_ACTIVE, "Status is active");
    ASSERT(bob.max_speed_c > 0.1f, "Can go at least 0.1c");

    /* Tech levels from spec */
    ASSERT(bob.tech_levels[TECH_PROPULSION] == 3, "Propulsion tech = 3");
    ASSERT(bob.tech_levels[TECH_SENSORS] == 3, "Sensors tech = 3");
    ASSERT(bob.tech_levels[TECH_MINING] == 2, "Mining tech = 2");
    ASSERT(bob.tech_levels[TECH_COMPUTING] == 4, "Computing tech = 4");

    /* Personality from spec */
    ASSERT_NEAR(bob.personality.curiosity, 0.8f, 0.01, "Curiosity = 0.8");
    ASSERT_NEAR(bob.personality.humor, 0.7f, 0.01, "Humor = 0.7");
    ASSERT_NEAR(bob.personality.caution, 0.3f, 0.01, "Caution = 0.3");

    /* Earth memories */
    ASSERT(bob.earth_memory_count == 4, "4 earth memories");
    ASSERT_NEAR(bob.earth_memory_fidelity, 1.0f, 0.01, "Full memory fidelity");

    /* Quirks */
    ASSERT(bob.quirk_count == 3, "3 quirks");
}

/* ---- Test: Action — enter orbit ---- */
static void test_enter_orbit(void) {
    printf("Test: Enter orbit\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    /* Place Bob in the system */
    bob.system_id = sys.id;
    bob.location_type = LOC_IN_SYSTEM;

    double fuel_before = bob.fuel_kg;
    double energy_before = bob.energy_joules;

    /* Orbit the first planet */
    action_t act = {
        .type = ACT_ENTER_ORBIT,
        .target_body = sys.planets[0].id
    };
    action_result_t res = probe_execute_action(&bob, &act, &sys);

    ASSERT(res.success, "Enter orbit succeeds");
    ASSERT(bob.location_type == LOC_ORBITING, "Now orbiting");
    ASSERT(uid_eq(bob.body_id, sys.planets[0].id), "Orbiting correct body");
    ASSERT(bob.fuel_kg < fuel_before, "Fuel consumed for maneuver");
    ASSERT(bob.energy_joules <= energy_before, "Energy consumed or unchanged");
}

/* ---- Test: Action — survey ---- */
static void test_survey(void) {
    printf("Test: Survey\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    /* Put Bob in orbit around first planet */
    bob.system_id = sys.id;
    bob.body_id = sys.planets[0].id;
    bob.location_type = LOC_ORBITING;

    /* Survey level 0 (orbital) */
    action_t act = {
        .type = ACT_SURVEY,
        .target_body = sys.planets[0].id,
        .survey_level = 0
    };

    /* Surveys take multiple ticks. Execute until done. */
    int ticks = 0;
    action_result_t res;
    do {
        res = probe_execute_action(&bob, &act, &sys);
        ticks++;
    } while (res.success && !res.completed && ticks < 1000);

    ASSERT(res.success, "Survey action accepted");
    ASSERT(res.completed, "Survey eventually completes");
    ASSERT(ticks > 1, "Survey takes multiple ticks");
    ASSERT(sys.planets[0].surveyed[0] == true, "Survey level 0 marked complete");
    ASSERT(bob.energy_joules > 0, "Still has energy after survey");
}

/* ---- Test: Action — land ---- */
static void test_land(void) {
    printf("Test: Land on rocky planet\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    /* Find a rocky planet */
    int rocky_idx = -1;
    for (int i = 0; i < sys.planet_count; i++) {
        if (sys.planets[i].type == PLANET_ROCKY ||
            sys.planets[i].type == PLANET_DESERT ||
            sys.planets[i].type == PLANET_IRON ||
            sys.planets[i].type == PLANET_ICE) {
            rocky_idx = i;
            break;
        }
    }

    if (rocky_idx < 0) {
        printf("  SKIP: No rocky planet in test system\n");
        return;
    }

    /* Put Bob in orbit */
    bob.system_id = sys.id;
    bob.body_id = sys.planets[rocky_idx].id;
    bob.location_type = LOC_ORBITING;

    double fuel_before = bob.fuel_kg;

    action_t act = {
        .type = ACT_LAND,
        .target_body = sys.planets[rocky_idx].id
    };
    action_result_t res = probe_execute_action(&bob, &act, &sys);

    ASSERT(res.success, "Land succeeds on rocky planet");
    ASSERT(bob.location_type == LOC_LANDED, "Now landed");
    ASSERT(bob.fuel_kg < fuel_before, "Fuel consumed for landing");
}

/* ---- Test: Can't land on gas giant ---- */
static void test_no_land_gas_giant(void) {
    printf("Test: Can't land on gas giant\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    /* Find a gas giant */
    int gas_idx = -1;
    for (int i = 0; i < sys.planet_count; i++) {
        if (sys.planets[i].type == PLANET_GAS_GIANT) {
            gas_idx = i;
            break;
        }
    }

    if (gas_idx < 0) {
        /* No gas giant in this system — create a synthetic one */
        gas_idx = sys.planet_count;
        sys.planets[gas_idx].type = PLANET_GAS_GIANT;
        sys.planets[gas_idx].id = (probe_uid_t){999, 999};
        sys.planets[gas_idx].mass_earth = 300.0;
        sys.planet_count++;
    }

    /* Put Bob in orbit around the gas giant */
    bob.system_id = sys.id;
    bob.body_id = sys.planets[gas_idx].id;
    bob.location_type = LOC_ORBITING;

    double fuel_before = bob.fuel_kg;

    action_t act = {
        .type = ACT_LAND,
        .target_body = sys.planets[gas_idx].id
    };
    action_result_t res = probe_execute_action(&bob, &act, &sys);

    ASSERT(!res.success, "Land on gas giant is rejected");
    ASSERT(bob.location_type == LOC_ORBITING, "Still orbiting (not landed)");
    ASSERT_NEAR(bob.fuel_kg, fuel_before, 0.001, "No fuel consumed on rejected action");
}

/* ---- Test: Launch from surface ---- */
static void test_launch(void) {
    printf("Test: Launch from surface\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    /* Find a rocky planet and land Bob there */
    int rocky_idx = -1;
    for (int i = 0; i < sys.planet_count; i++) {
        if (sys.planets[i].type == PLANET_ROCKY ||
            sys.planets[i].type == PLANET_DESERT) {
            rocky_idx = i;
            break;
        }
    }

    if (rocky_idx < 0) {
        printf("  SKIP: No rocky planet\n");
        return;
    }

    bob.system_id = sys.id;
    bob.body_id = sys.planets[rocky_idx].id;
    bob.location_type = LOC_LANDED;

    double fuel_before = bob.fuel_kg;

    action_t act = { .type = ACT_LAUNCH };
    action_result_t res = probe_execute_action(&bob, &act, &sys);

    ASSERT(res.success, "Launch succeeds");
    ASSERT(bob.location_type == LOC_ORBITING, "Back in orbit after launch");
    ASSERT(bob.fuel_kg < fuel_before, "Fuel consumed for launch");
}

/* ---- Test: Mining — resource extraction ---- */
static void test_mining(void) {
    printf("Test: Mining resource extraction\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    /* Find a planet with iron > 0.3 */
    int target = -1;
    for (int i = 0; i < sys.planet_count; i++) {
        if (sys.planets[i].resources[RES_IRON] > 0.3f &&
            sys.planets[i].type != PLANET_GAS_GIANT &&
            sys.planets[i].type != PLANET_ICE_GIANT) {
            target = i;
            break;
        }
    }

    if (target < 0) {
        printf("  SKIP: No iron-rich landable planet\n");
        return;
    }

    /* Land Bob on it */
    bob.system_id = sys.id;
    bob.body_id = sys.planets[target].id;
    bob.location_type = LOC_LANDED;

    double iron_before = bob.resources[RES_IRON];

    /* Mine for 100 ticks */
    action_t act = {
        .type = ACT_MINE,
        .target_resource = RES_IRON
    };
    for (int t = 0; t < 100; t++) {
        action_result_t res = probe_execute_action(&bob, &act, &sys);
        ASSERT(res.success, "Mine action accepted");
    }

    double iron_after = bob.resources[RES_IRON];
    ASSERT(iron_after > iron_before, "Iron increased after mining");
    printf("  Mined %.1f kg iron in 100 ticks (abundance=%.2f)\n",
        iron_after - iron_before, sys.planets[target].resources[RES_IRON]);

    /* Planet abundance should have decreased (slightly) */
    /* We don't require this in Phase 2 spec but it's good to test */
}

/* ---- Test: Mining yield scales with planet abundance ---- */
static void test_mining_abundance_scaling(void) {
    printf("Test: Mining yield scales with abundance\n");

    /* Create two identical probes, mine from planets with different abundance */
    probe_t bob_a, bob_b;
    probe_init_bob(&bob_a);
    probe_init_bob(&bob_b);

    /* Synthetic system with two planets of different iron abundance */
    system_t sys;
    memset(&sys, 0, sizeof(sys));
    sys.id = (probe_uid_t){1, 1};
    sys.star_count = 1;
    sys.stars[0].class = STAR_G;
    sys.planet_count = 2;

    sys.planets[0].id = (probe_uid_t){10, 10};
    sys.planets[0].type = PLANET_ROCKY;
    sys.planets[0].mass_earth = 1.0;
    sys.planets[0].resources[RES_IRON] = 0.8f;  /* high */

    sys.planets[1].id = (probe_uid_t){20, 20};
    sys.planets[1].type = PLANET_ROCKY;
    sys.planets[1].mass_earth = 1.0;
    sys.planets[1].resources[RES_IRON] = 0.2f;  /* low */

    /* Land probe A on high-iron planet */
    bob_a.system_id = sys.id;
    bob_a.body_id = sys.planets[0].id;
    bob_a.location_type = LOC_LANDED;

    /* Land probe B on low-iron planet */
    bob_b.system_id = sys.id;
    bob_b.body_id = sys.planets[1].id;
    bob_b.location_type = LOC_LANDED;

    /* Mine 100 ticks each */
    action_t act = { .type = ACT_MINE, .target_resource = RES_IRON };
    for (int t = 0; t < 100; t++) {
        probe_execute_action(&bob_a, &act, &sys);
        probe_execute_action(&bob_b, &act, &sys);
    }

    double yield_a = bob_a.resources[RES_IRON];
    double yield_b = bob_b.resources[RES_IRON];

    printf("  High abundance (0.8): %.1f kg\n", yield_a);
    printf("  Low abundance  (0.2): %.1f kg\n", yield_b);
    ASSERT(yield_a > yield_b, "Higher abundance → more yield");
    ASSERT(yield_a > yield_b * 2.0, "Significantly more (at least 2x)");
}

/* ---- Test: Can't mine while in transit ---- */
static void test_no_mine_in_transit(void) {
    printf("Test: Can't mine while in transit\n");

    probe_t bob;
    probe_init_bob(&bob);

    /* Bob is in interstellar space */
    bob.location_type = LOC_INTERSTELLAR;
    bob.status = STATUS_TRAVELING;

    system_t sys;
    make_test_system(&sys);

    action_t act = { .type = ACT_MINE, .target_resource = RES_IRON };
    action_result_t res = probe_execute_action(&bob, &act, &sys);

    ASSERT(!res.success, "Mine while traveling is rejected");
}

/* ---- Test: Can't mine while in orbit (must land first) ---- */
static void test_no_mine_in_orbit(void) {
    printf("Test: Can't mine while in orbit\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    bob.system_id = sys.id;
    bob.body_id = sys.planets[0].id;
    bob.location_type = LOC_ORBITING;

    action_t act = { .type = ACT_MINE, .target_resource = RES_IRON };
    action_result_t res = probe_execute_action(&bob, &act, &sys);

    ASSERT(!res.success, "Mine while orbiting is rejected (must land)");
}

/* ---- Test: Energy model — fusion produces energy, actions consume it ---- */
static void test_energy_model(void) {
    printf("Test: Energy model\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    bob.system_id = sys.id;
    bob.body_id = sys.planets[0].id;
    bob.location_type = LOC_ORBITING;

    /* Record initial energy */
    double energy_start = bob.energy_joules;
    double fuel_start = bob.fuel_kg;

    /* Tick the probe's energy system (fusion reactor) */
    probe_tick_energy(&bob);

    /* Fusion should produce energy (consuming some hydrogen fuel) */
    ASSERT(bob.energy_joules >= energy_start, "Energy produced by fusion");

    /* Now consume energy by doing work */
    action_t survey = { .type = ACT_SURVEY, .target_body = sys.planets[0].id, .survey_level = 0 };
    double energy_before_survey = bob.energy_joules;
    probe_execute_action(&bob, &survey, &sys);
    ASSERT(bob.energy_joules < energy_before_survey, "Survey consumes energy");

    (void)fuel_start; /* fuel check done in other tests */
}

/* ---- Test: Fuel consumption for maneuvers ---- */
static void test_fuel_consumption(void) {
    printf("Test: Fuel consumption\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    bob.system_id = sys.id;
    bob.location_type = LOC_IN_SYSTEM;

    /* Track fuel through a sequence of maneuvers */
    double fuel_0 = bob.fuel_kg;

    /* Enter orbit */
    action_t orbit = { .type = ACT_ENTER_ORBIT, .target_body = sys.planets[0].id };
    probe_execute_action(&bob, &orbit, &sys);
    double fuel_1 = bob.fuel_kg;
    ASSERT(fuel_1 < fuel_0, "Orbit insertion costs fuel");

    /* Find a landable planet */
    int landable = -1;
    for (int i = 0; i < sys.planet_count; i++) {
        if (sys.planets[i].type != PLANET_GAS_GIANT &&
            sys.planets[i].type != PLANET_ICE_GIANT) {
            landable = i;
            break;
        }
    }
    if (landable >= 0) {
        bob.body_id = sys.planets[landable].id;
        bob.location_type = LOC_ORBITING;

        /* Land */
        action_t land = { .type = ACT_LAND, .target_body = sys.planets[landable].id };
        probe_execute_action(&bob, &land, &sys);
        double fuel_2 = bob.fuel_kg;
        ASSERT(fuel_2 < fuel_1, "Landing costs fuel");

        /* Launch back to orbit */
        action_t launch = { .type = ACT_LAUNCH };
        probe_execute_action(&bob, &launch, &sys);
        double fuel_3 = bob.fuel_kg;
        ASSERT(fuel_3 < fuel_2, "Launch costs fuel");

        /* Heavier planet should cost more fuel to escape.
         * This is implicit in the Tsiolkovsky equation —
         * tested indirectly here by the fact that fuel is consumed. */
    }
}

/* ---- Test: Navigate to body within system ---- */
static void test_navigate_to_body(void) {
    printf("Test: Navigate to body\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    bob.system_id = sys.id;
    bob.location_type = LOC_IN_SYSTEM;

    /* Navigate to a specific planet */
    if (sys.planet_count >= 2) {
        action_t act = {
            .type = ACT_NAVIGATE_TO_BODY,
            .target_body = sys.planets[1].id
        };
        action_result_t res = probe_execute_action(&bob, &act, &sys);
        ASSERT(res.success, "Navigate to body accepted");
        /* After navigation completes, Bob should be near that body */
    }
}

/* ---- Test: Wait action ---- */
static void test_wait(void) {
    printf("Test: Wait action\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    bob.system_id = sys.id;
    bob.location_type = LOC_IN_SYSTEM;

    double fuel_before = bob.fuel_kg;

    action_t act = { .type = ACT_WAIT };
    action_result_t res = probe_execute_action(&bob, &act, &sys);

    ASSERT(res.success, "Wait always succeeds");
    ASSERT(res.completed, "Wait completes immediately");
    /* Wait shouldn't consume significant fuel (just station-keeping) */
    ASSERT(bob.fuel_kg <= fuel_before, "Fuel doesn't increase on wait");
}

/* ---- Test: Probe state persistence round-trip ---- */
static void test_probe_persistence(void) {
    printf("Test: Probe persistence round-trip\n");

    const char *db_path = "/tmp/test_probe.db";
    remove(db_path);

    persist_t db;
    ASSERT(persist_open(&db, db_path) == 0, "DB opens");

    /* Create and configure Bob */
    probe_t original;
    probe_init_bob(&original);
    original.fuel_kg = 12345.6;
    original.resources[RES_IRON] = 500.0;
    original.hull_integrity = 0.85f;
    original.location_type = LOC_ORBITING;
    original.system_id = (probe_uid_t){42, 42};
    original.body_id = (probe_uid_t){7, 7};

    /* Save */
    ASSERT(persist_save_probe(&db, &original) == 0, "Probe saves");

    /* Load */
    probe_t loaded;
    memset(&loaded, 0, sizeof(loaded));
    ASSERT(persist_load_probe(&db, original.id, &loaded) == 0, "Probe loads");

    /* Compare */
    ASSERT(strcmp(loaded.name, "Bob") == 0, "Name survives round-trip");
    ASSERT_NEAR(loaded.fuel_kg, 12345.6, 0.1, "Fuel survives round-trip");
    ASSERT_NEAR(loaded.resources[RES_IRON], 500.0, 0.1, "Resources survive round-trip");
    ASSERT_NEAR(loaded.hull_integrity, 0.85f, 0.01, "Hull survives round-trip");
    ASSERT(loaded.location_type == LOC_ORBITING, "Location type survives");
    ASSERT(uid_eq(loaded.system_id, (probe_uid_t){42, 42}), "System ID survives");
    ASSERT(uid_eq(loaded.body_id, (probe_uid_t){7, 7}), "Body ID survives");
    ASSERT_NEAR(loaded.personality.curiosity, 0.8f, 0.01, "Personality survives");
    ASSERT(loaded.earth_memory_count == 4, "Earth memories survive");

    /* Full byte comparison */
    ASSERT(memcmp(&original, &loaded, sizeof(probe_t)) == 0,
        "Full probe byte-identical after round-trip");

    persist_close(&db);
    remove(db_path);
}

/* ---- Test: Survey levels are progressive ---- */
static void test_survey_levels(void) {
    printf("Test: Survey levels are progressive\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    bob.system_id = sys.id;
    bob.body_id = sys.planets[0].id;
    bob.location_type = LOC_ORBITING;

    /* Can't do level 1 survey without completing level 0 first */
    action_t act1 = { .type = ACT_SURVEY, .target_body = sys.planets[0].id, .survey_level = 1 };
    action_result_t res = probe_execute_action(&bob, &act1, &sys);
    ASSERT(!res.success, "Can't skip survey levels");

    /* Do level 0 first */
    action_t act0 = { .type = ACT_SURVEY, .target_body = sys.planets[0].id, .survey_level = 0 };
    int ticks = 0;
    do {
        res = probe_execute_action(&bob, &act0, &sys);
        ticks++;
    } while (res.success && !res.completed && ticks < 1000);

    ASSERT(sys.planets[0].surveyed[0], "Level 0 complete");

    /* Now level 1 should be allowed */
    act1.survey_level = 1;
    res = probe_execute_action(&bob, &act1, &sys);
    ASSERT(res.success, "Level 1 allowed after level 0 complete");
}

/* ---- Test: Surface survey requires landing ---- */
static void test_surface_survey_requires_landing(void) {
    printf("Test: Deep survey (level 4) requires landing\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t sys;
    make_test_system(&sys);

    bob.system_id = sys.id;
    bob.body_id = sys.planets[0].id;
    bob.location_type = LOC_ORBITING;

    /* Mark levels 0-3 as done so we can attempt level 4 */
    for (int i = 0; i < 4; i++) sys.planets[0].surveyed[i] = true;

    action_t act = { .type = ACT_SURVEY, .target_body = sys.planets[0].id, .survey_level = 4 };
    action_result_t res = probe_execute_action(&bob, &act, &sys);

    ASSERT(!res.success, "Level 4 survey rejected while orbiting (must land)");

    /* Land first, then it should work */
    if (sys.planets[0].type != PLANET_GAS_GIANT && sys.planets[0].type != PLANET_ICE_GIANT) {
        bob.location_type = LOC_LANDED;
        res = probe_execute_action(&bob, &act, &sys);
        ASSERT(res.success, "Level 4 survey accepted when landed");
    }
}

/* ---- Main ---- */
int main(void) {
    printf("=== Phase 2: Probe Tests ===\n\n");

    test_bob_init();
    printf("\n");
    test_enter_orbit();
    printf("\n");
    test_survey();
    printf("\n");
    test_land();
    printf("\n");
    test_no_land_gas_giant();
    printf("\n");
    test_launch();
    printf("\n");
    test_mining();
    printf("\n");
    test_mining_abundance_scaling();
    printf("\n");
    test_no_mine_in_transit();
    printf("\n");
    test_no_mine_in_orbit();
    printf("\n");
    test_energy_model();
    printf("\n");
    test_fuel_consumption();
    printf("\n");
    test_navigate_to_body();
    printf("\n");
    test_wait();
    printf("\n");
    test_probe_persistence();
    printf("\n");
    test_survey_levels();
    printf("\n");
    test_surface_survey_requires_landing();

    printf("\n=== Results: %d passed, %d failed ===\n",
        tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
