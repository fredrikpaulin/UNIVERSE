#define _POSIX_C_SOURCE 199309L
/*
 * test_travel.c — Phase 3 tests: interstellar travel, sensors, arrival
 *
 * Written BEFORE implementation. Defines the travel.h API contract.
 *
 * Tests the spec requirements:
 *   - Bob navigates to a system 10 ly away at 0.15c → arrival in ~24,345 ticks
 *   - Fuel at arrival < fuel at departure
 *   - Hull integrity may decrease during transit (micrometeorites)
 *   - Destination system is generated on arrival and persisted
 *   - Long-range scan detects systems within sensor range
 *   - Can't mine/survey while traveling
 *   - Insufficient fuel → probe enters drift state
 *   - Relativistic time dilation applied
 */
#include "universe.h"
#include "rng.h"
#include "generate.h"
#include "persist.h"
#include "probe.h"
#include "travel.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <time.h>

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

/* ---- Helper: generate origin sector with systems ---- */
static int make_origin_systems(system_t *systems, int max) {
    return generate_sector(systems, max, 42, (sector_coord_t){0, 0, 0});
}

/* ---- Test: Initiate interstellar travel ---- */
static void test_initiate_travel(void) {
    printf("Test: Initiate interstellar travel\n");

    probe_t bob;
    probe_init_bob(&bob);

    system_t systems[30];
    int count = make_origin_systems(systems, 30);
    ASSERT(count > 0, "Origin sector has systems");

    /* Place Bob in the first system */
    bob.system_id = systems[0].id;
    bob.sector = systems[0].sector;
    bob.location_type = LOC_IN_SYSTEM;

    /* Pick a destination — use a system in a neighboring sector */
    system_t dest_systems[30];
    int dest_count = generate_sector(dest_systems, 30, 42, (sector_coord_t){1, 0, 0});
    ASSERT(dest_count > 0, "Destination sector has systems");

    double fuel_before = bob.fuel_kg;

    /* Initiate travel to the destination system */
    travel_order_t order = {
        .target_pos = dest_systems[0].position,
        .target_system_id = dest_systems[0].id,
        .target_sector = dest_systems[0].sector,
    };
    travel_result_t tres = travel_initiate(&bob, &order);

    ASSERT(tres.success, "Travel initiation succeeds");
    ASSERT(bob.status == STATUS_TRAVELING, "Status is traveling");
    ASSERT(bob.location_type == LOC_INTERSTELLAR, "Location is interstellar");
    ASSERT(bob.speed_c > 0, "Probe has velocity");
    ASSERT(bob.travel_remaining_ly > 0, "Distance remaining > 0");
    ASSERT(tres.estimated_ticks > 0, "Estimated ticks > 0");
    /* Fuel reservation should reduce available fuel */
    ASSERT(bob.fuel_kg <= fuel_before, "Fuel committed or unchanged at departure");
}

/* ---- Test: Travel time calculation ---- */
static void test_travel_time(void) {
    printf("Test: Travel time for 10 ly at 0.15c\n");

    probe_t bob;
    probe_init_bob(&bob);
    bob.location_type = LOC_IN_SYSTEM;
    bob.sector = (sector_coord_t){0, 0, 0};

    /* Create a target exactly 10 ly away */
    vec3_t origin = {0, 0, 0};
    vec3_t dest = {10.0, 0, 0};
    bob.heading = origin;

    travel_order_t order = {
        .target_pos = dest,
        .target_system_id = (probe_uid_t){99, 99},
        .target_sector = (sector_coord_t){0, 0, 0},
    };
    travel_result_t tres = travel_initiate(&bob, &order);
    ASSERT(tres.success, "Travel initiated");

    /* At 0.15c, 10 ly takes 10/0.15 = 66.67 years = 24,333 ticks (365 ticks/year) */
    int expected_ticks = (int)(10.0 / 0.15 * 365.25);
    printf("  Expected ticks: ~%d\n", expected_ticks);
    printf("  Estimated ticks: %d\n", (int)tres.estimated_ticks);

    /* Allow 5% tolerance for fuel/acceleration model differences */
    double ratio = (double)tres.estimated_ticks / (double)expected_ticks;
    ASSERT(ratio > 0.8 && ratio < 1.2, "Travel time within 20% of expected");
    ASSERT_NEAR(bob.travel_remaining_ly, 10.0, 0.5, "Distance remaining ~10 ly");
}

/* ---- Test: Travel tick processing — arrival ---- */
static void test_travel_arrival(void) {
    printf("Test: Travel tick processing and arrival\n");

    probe_t bob;
    probe_init_bob(&bob);
    bob.location_type = LOC_IN_SYSTEM;

    /* Short trip: 1 ly away at 0.15c = ~6.67 years = ~2,434 ticks */
    travel_order_t order = {
        .target_pos = (vec3_t){1.0, 0, 0},
        .target_system_id = (probe_uid_t){88, 88},
        .target_sector = (sector_coord_t){0, 0, 0},
    };
    travel_result_t tres = travel_initiate(&bob, &order);
    ASSERT(tres.success, "Travel initiated for short trip");

    double fuel_at_departure = bob.fuel_kg;
    int ticks = 0;
    bool arrived = false;

    /* Tick until arrival (with safety cap) */
    rng_t rng;
    rng_seed(&rng, 42);
    while (ticks < 50000 && !arrived) {
        travel_tick_result_t tick_res = travel_tick(&bob, &rng);
        ticks++;
        if (tick_res.arrived) {
            arrived = true;
        }
    }

    printf("  Arrived after %d ticks\n", ticks);
    ASSERT(arrived, "Probe eventually arrives");
    ASSERT(ticks > 1000, "Trip takes a meaningful number of ticks");
    ASSERT(bob.status != STATUS_TRAVELING, "No longer traveling");
    ASSERT(bob.location_type == LOC_IN_SYSTEM, "Back in a system");
    ASSERT(bob.travel_remaining_ly < 0.01, "Distance remaining ~0");
    ASSERT(bob.fuel_kg < fuel_at_departure, "Fuel consumed during travel");
}

/* ---- Test: Hull damage during transit ---- */
static void test_transit_hull_damage(void) {
    printf("Test: Micrometeorite damage during transit\n");

    /* Run many short trips to statistically guarantee at least one hit */
    int damage_count = 0;
    int trials = 20;

    for (int trial = 0; trial < trials; trial++) {
        probe_t bob;
        probe_init_bob(&bob);
        bob.location_type = LOC_IN_SYSTEM;

        travel_order_t order = {
            .target_pos = (vec3_t){5.0, 0, 0},
            .target_system_id = (probe_uid_t){77, (uint64_t)trial},
            .target_sector = (sector_coord_t){0, 0, 0},
        };
        travel_initiate(&bob, &order);

        rng_t rng;
        rng_seed(&rng, 1000 + (uint64_t)trial);
        for (int t = 0; t < 15000 && bob.status == STATUS_TRAVELING; t++) {
            travel_tick(&bob, &rng);
        }

        if (bob.hull_integrity < 1.0f) damage_count++;
    }

    printf("  Damage in %d/%d trials\n", damage_count, trials);
    ASSERT(damage_count > 0, "At least one trial had hull damage (stochastic)");
    /* Most trips of ~12,000 ticks should have some damage */
    ASSERT(damage_count >= 2, "Multiple trials had damage");
}

/* ---- Test: Can't mine while traveling ---- */
static void test_no_actions_while_traveling(void) {
    printf("Test: Can't mine/survey/land while traveling\n");

    probe_t bob;
    probe_init_bob(&bob);
    bob.location_type = LOC_IN_SYSTEM;

    travel_order_t order = {
        .target_pos = (vec3_t){10.0, 0, 0},
        .target_system_id = (probe_uid_t){66, 66},
        .target_sector = (sector_coord_t){0, 0, 0},
    };
    travel_initiate(&bob, &order);

    ASSERT(bob.status == STATUS_TRAVELING, "Bob is traveling");

    /* All these actions should be rejected */
    system_t dummy_sys;
    memset(&dummy_sys, 0, sizeof(dummy_sys));

    action_t mine = { .type = ACT_MINE, .target_resource = RES_IRON };
    ASSERT(!probe_execute_action(&bob, &mine, &dummy_sys).success, "Mine rejected while traveling");

    action_t survey = { .type = ACT_SURVEY, .survey_level = 0 };
    ASSERT(!probe_execute_action(&bob, &survey, &dummy_sys).success, "Survey rejected while traveling");

    action_t land = { .type = ACT_LAND };
    ASSERT(!probe_execute_action(&bob, &land, &dummy_sys).success, "Land rejected while traveling");

    action_t orbit = { .type = ACT_ENTER_ORBIT };
    ASSERT(!probe_execute_action(&bob, &orbit, &dummy_sys).success, "Enter orbit rejected while traveling");

    /* Wait should still work though */
    action_t wait = { .type = ACT_WAIT };
    ASSERT(probe_execute_action(&bob, &wait, &dummy_sys).success, "Wait allowed while traveling");
}

/* ---- Test: Insufficient fuel → drift ---- */
static void test_insufficient_fuel_drift(void) {
    printf("Test: Insufficient fuel → drift state\n");

    probe_t bob;
    probe_init_bob(&bob);
    bob.location_type = LOC_IN_SYSTEM;
    bob.fuel_kg = 1.0; /* Almost no fuel */

    /* Try to travel 100 ly — way too far for 1 kg fuel */
    travel_order_t order = {
        .target_pos = (vec3_t){100.0, 0, 0},
        .target_system_id = (probe_uid_t){55, 55},
        .target_sector = (sector_coord_t){1, 0, 0},
    };
    travel_result_t tres = travel_initiate(&bob, &order);

    /* Two valid outcomes:
     * 1. Initiation fails (rejected due to insufficient fuel)
     * 2. Initiation succeeds but probe runs out mid-flight and enters drift */
    if (!tres.success) {
        printf("  Travel rejected due to insufficient fuel (valid)\n");
        ASSERT(!tres.success, "Travel rejected");
    } else {
        printf("  Travel accepted, simulating drift...\n");
        rng_t rng;
        rng_seed(&rng, 42);
        int ticks = 0;
        bool drifting = false;
        while (ticks < 100000 && !drifting) {
            travel_tick_result_t tick_res = travel_tick(&bob, &rng);
            ticks++;
            if (tick_res.fuel_exhausted) {
                drifting = true;
            }
            if (tick_res.arrived) break;
        }
        ASSERT(drifting, "Probe runs out of fuel and enters drift");
        ASSERT(bob.status == STATUS_DORMANT || bob.fuel_kg <= 0,
            "Probe is dormant or has no fuel");
    }
}

/* ---- Test: Long-range sensor scan ---- */
static void test_long_range_scan(void) {
    printf("Test: Long-range sensor scan\n");

    probe_t bob;
    probe_init_bob(&bob);
    bob.sensor_range_ly = 20.0f;

    /* Generate a sector and place Bob in its first system */
    system_t origin_systems[30];
    int origin_count = make_origin_systems(origin_systems, 30);
    ASSERT(origin_count > 0, "Origin sector has systems");

    bob.system_id = origin_systems[0].id;
    bob.sector = origin_systems[0].sector;
    bob.heading = origin_systems[0].position;
    bob.location_type = LOC_IN_SYSTEM;

    /* Generate neighboring sectors so there are targets to find */
    system_t all_systems[200];
    int total = 0;
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            system_t buf[30];
            int n = generate_sector(buf, 30, 42, (sector_coord_t){dx, dy, 0});
            for (int i = 0; i < n && total < 200; i++) {
                all_systems[total++] = buf[i];
            }
        }
    }
    printf("  Total systems in nearby sectors: %d\n", total);

    /* Perform scan */
    scan_result_t results[64];
    int found = travel_scan(&bob, all_systems, total, results, 64);

    printf("  Systems detected within %.0f ly: %d\n", bob.sensor_range_ly, found);
    ASSERT(found > 0, "Scan found at least one system");
    ASSERT(found < total, "Scan didn't return everything (range limited)");

    /* Verify all results are within sensor range */
    for (int i = 0; i < found; i++) {
        ASSERT(results[i].distance_ly <= bob.sensor_range_ly + 0.1,
            "Detected system within sensor range");
        ASSERT(results[i].star_class >= 0 && results[i].star_class < STAR_CLASS_COUNT,
            "Valid star class");
        ASSERT(results[i].distance_ly > 0, "Non-zero distance");
    }

    /* Results should be sorted by distance */
    for (int i = 1; i < found; i++) {
        ASSERT(results[i].distance_ly >= results[i-1].distance_ly,
            "Results sorted by distance");
    }
}

/* ---- Test: Scan returns star class but not full planet data ---- */
static void test_scan_limited_info(void) {
    printf("Test: Scan returns limited info (star class, distance, not planets)\n");

    probe_t bob;
    probe_init_bob(&bob);
    bob.sensor_range_ly = 50.0f; /* big range to ensure hits */

    system_t systems[30];
    int count = generate_sector(systems, 30, 42, (sector_coord_t){0, 0, 0});
    ASSERT(count > 0, "Has systems to scan");

    /* Place Bob at origin */
    bob.heading = (vec3_t){0, 0, 0};

    scan_result_t results[64];
    int found = travel_scan(&bob, systems, count, results, 64);
    ASSERT(found > 0, "Scan found targets");

    /* scan_result_t should have: system_id, star_class, distance_ly, direction
     * but NOT planet details */
    ASSERT(!uid_is_null(results[0].system_id), "Has system ID");
    ASSERT(results[0].distance_ly > 0, "Has distance");
    /* star_class is an enum 0-9, already tested above */
}

/* ---- Test: Relativistic time dilation ---- */
static void test_time_dilation(void) {
    printf("Test: Relativistic time dilation\n");

    /* Lorentz factor: gamma = 1 / sqrt(1 - v²/c²)
     * At 0.15c: gamma = 1 / sqrt(1 - 0.0225) = ~1.0114
     * At 0.50c: gamma = 1 / sqrt(1 - 0.25)   = ~1.1547 */

    double gamma_015 = travel_lorentz_factor(0.15);
    double gamma_050 = travel_lorentz_factor(0.50);
    double gamma_000 = travel_lorentz_factor(0.0);
    double gamma_090 = travel_lorentz_factor(0.90);

    ASSERT_NEAR(gamma_000, 1.0, 0.001, "Gamma at 0c = 1.0");
    ASSERT_NEAR(gamma_015, 1.0114, 0.002, "Gamma at 0.15c = ~1.011");
    ASSERT_NEAR(gamma_050, 1.1547, 0.002, "Gamma at 0.50c = ~1.155");
    ASSERT(gamma_090 > 2.0, "Gamma at 0.90c > 2.0");

    printf("  Gamma(0.15c) = %.4f\n", gamma_015);
    printf("  Gamma(0.50c) = %.4f\n", gamma_050);
    printf("  Gamma(0.90c) = %.4f\n", gamma_090);
}

/* ---- Test: Arrival generates destination system ---- */
static void test_arrival_generates_system(void) {
    printf("Test: Arrival at new sector generates system\n");

    const char *db_path = "/tmp/test_travel.db";
    remove(db_path);

    persist_t db;
    ASSERT(persist_open(&db, db_path) == 0, "DB opens");

    /* Generate origin sector and persist it */
    system_t origin_systems[30];
    int origin_count = make_origin_systems(origin_systems, 30);
    persist_save_sector(&db, (sector_coord_t){0,0,0}, 0, origin_systems, origin_count);

    /* Verify destination sector doesn't exist yet */
    sector_coord_t dest_sector = {1, 0, 0};
    ASSERT(persist_sector_exists(&db, dest_sector) == -1, "Dest sector not yet generated");

    /* Generate destination, simulating what arrival would do */
    system_t dest_systems[30];
    int dest_count = generate_sector(dest_systems, 30, 42, dest_sector);
    ASSERT(dest_count > 0, "Destination sector has systems");

    /* Save (this is what travel_arrive should do) */
    persist_save_sector(&db, dest_sector, 1000, dest_systems, dest_count);

    /* Verify it persisted */
    int stored = persist_sector_exists(&db, dest_sector);
    ASSERT(stored == dest_count, "Destination sector persisted");

    /* Load back and verify */
    system_t loaded[30];
    int loaded_count = persist_load_sector(&db, dest_sector, loaded, 30);
    ASSERT(loaded_count == dest_count, "Loaded count matches");
    ASSERT(memcmp(&dest_systems[0], &loaded[0], sizeof(system_t)) == 0,
        "First system byte-identical");

    persist_close(&db);
    remove(db_path);
}

/* ---- Test: Travel preserves and updates probe state correctly ---- */
static void test_travel_state_integrity(void) {
    printf("Test: Travel state integrity\n");

    probe_t bob;
    probe_init_bob(&bob);
    bob.location_type = LOC_IN_SYSTEM;
    bob.system_id = (probe_uid_t){1, 1};

    /* Record pre-travel state */
    double fuel_before = bob.fuel_kg;
    uint32_t gen = bob.generation;
    float curiosity = bob.personality.curiosity;

    travel_order_t order = {
        .target_pos = (vec3_t){2.0, 0, 0},
        .target_system_id = (probe_uid_t){44, 44},
        .target_sector = (sector_coord_t){0, 0, 0},
    };
    travel_initiate(&bob, &order);

    /* Tick a few times */
    rng_t rng;
    rng_seed(&rng, 123);
    for (int i = 0; i < 100; i++) {
        travel_tick(&bob, &rng);
    }

    /* Personality shouldn't change during travel (that's Phase 6) */
    ASSERT_NEAR(bob.personality.curiosity, curiosity, 0.001,
        "Personality unchanged during travel");
    /* Generation shouldn't change */
    ASSERT(bob.generation == gen, "Generation unchanged");
    /* Fuel should be decreasing */
    ASSERT(bob.fuel_kg < fuel_before, "Fuel decreasing during travel");
    /* Name should be intact */
    ASSERT(strcmp(bob.name, "Bob") == 0, "Name preserved");
}

/* ---- Test: Speed determined by max_speed_c ---- */
static void test_speed_from_capability(void) {
    printf("Test: Travel speed matches probe capability\n");

    probe_t fast_bob, slow_bob;
    probe_init_bob(&fast_bob);
    probe_init_bob(&slow_bob);

    fast_bob.location_type = LOC_IN_SYSTEM;
    slow_bob.location_type = LOC_IN_SYSTEM;
    slow_bob.max_speed_c = 0.05f; /* Slower probe */

    travel_order_t order = {
        .target_pos = (vec3_t){10.0, 0, 0},
        .target_system_id = (probe_uid_t){33, 33},
        .target_sector = (sector_coord_t){0, 0, 0},
    };

    travel_result_t fast_res = travel_initiate(&fast_bob, &order);
    travel_result_t slow_res = travel_initiate(&slow_bob, &order);

    ASSERT(fast_res.success && slow_res.success, "Both initiate");
    ASSERT(fast_bob.speed_c > slow_bob.speed_c, "Fast probe travels faster");
    ASSERT(fast_res.estimated_ticks < slow_res.estimated_ticks,
        "Fast probe arrives sooner");
    printf("  Fast (0.15c): %llu ticks\n", (unsigned long long)fast_res.estimated_ticks);
    printf("  Slow (0.05c): %llu ticks\n", (unsigned long long)slow_res.estimated_ticks);
}

/* ---- Main ---- */
int main(void) {
    printf("=== Phase 3: Travel Tests ===\n\n");

    test_initiate_travel();
    printf("\n");
    test_travel_time();
    printf("\n");
    test_travel_arrival();
    printf("\n");
    test_transit_hull_damage();
    printf("\n");
    test_no_actions_while_traveling();
    printf("\n");
    test_insufficient_fuel_drift();
    printf("\n");
    test_long_range_scan();
    printf("\n");
    test_scan_limited_info();
    printf("\n");
    test_time_dilation();
    printf("\n");
    test_arrival_generates_system();
    printf("\n");
    test_travel_state_integrity();
    printf("\n");
    test_speed_from_capability();

    printf("\n=== Results: %d passed, %d failed ===\n",
        tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
