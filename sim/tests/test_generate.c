#define _POSIX_C_SOURCE 199309L
/*
 * test_generate.c — Phase 1 verification tests
 *
 * Tests: determinism, star class distribution, habitable zone math,
 *        planet generation, sector density, persistence round-trip.
 */
#include "universe.h"
#include "rng.h"
#include "generate.h"
#include "persist.h"
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

#define ASSERT_NEAR(a, b, tol, msg) ASSERT(fabs((a)-(b)) < (tol), msg)

/* ---- Test: PRNG determinism ---- */
static void test_rng_determinism(void) {
    printf("Test: RNG determinism\n");
    rng_t a, b;
    rng_seed(&a, 42);
    rng_seed(&b, 42);
    for (int i = 0; i < 10000; i++) {
        uint64_t va = rng_next(&a);
        uint64_t vb = rng_next(&b);
        if (va != vb) {
            fprintf(stderr, "  FAIL: RNG diverged at step %d\n", i);
            tests_failed++;
            return;
        }
    }
    tests_passed++;
    printf("  PASS: 10000 values identical from same seed\n");
}

/* ---- Test: Sector generation determinism ---- */
static void test_sector_determinism(void) {
    printf("Test: Sector generation determinism\n");
    system_t sys_a[30], sys_b[30];
    sector_coord_t coord = {5, 5, 0};

    int count_a = generate_sector(sys_a, 30, 42, coord);
    int count_b = generate_sector(sys_b, 30, 42, coord);

    ASSERT(count_a == count_b, "Same seed produces same system count");
    ASSERT(count_a > 0, "Sector has at least one system");

    /* Compare every byte */
    int match = (memcmp(sys_a, sys_b, sizeof(system_t) * (size_t)count_a) == 0);
    ASSERT(match, "All system data byte-identical");
    printf("  Generated %d systems in sector (5,5,0)\n", count_a);
}

/* ---- Test: Star class distribution ---- */
static void test_star_distribution(void) {
    printf("Test: Star class distribution (100 sectors)\n");
    int class_counts[STAR_CLASS_COUNT] = {0};
    int total_stars = 0;

    /* Generate 100 sectors near the galactic plane */
    for (int sx = -5; sx < 5; sx++) {
        for (int sy = -5; sy < 5; sy++) {
            system_t systems[30];
            sector_coord_t coord = {sx, sy, 0};
            int count = generate_sector(systems, 30, 42, coord);
            for (int i = 0; i < count; i++) {
                for (int s = 0; s < systems[i].star_count; s++) {
                    class_counts[systems[i].stars[s].class]++;
                    total_stars++;
                }
            }
        }
    }

    printf("  Total stars: %d\n", total_stars);
    printf("  Distribution:\n");
    const char *names[] = {"O","B","A","F","G","K","M","WD","NS","BH"};
    for (int i = 0; i < STAR_CLASS_COUNT; i++) {
        double pct = (double)class_counts[i] / total_stars * 100.0;
        printf("    %2s: %5d (%5.2f%%)\n", names[i], class_counts[i], pct);
    }

    ASSERT(total_stars > 100, "Enough stars for statistical test");
    /* M-type should dominate (>50% is reasonable given multi-type systems) */
    double m_pct = (double)class_counts[STAR_M] / total_stars;
    ASSERT(m_pct > 0.40, "M-type stars are most common (>40%)");
    /* O-type should be very rare */
    double o_pct = (double)class_counts[STAR_O] / total_stars;
    ASSERT(o_pct < 0.02, "O-type stars are very rare (<2%)");
}

/* ---- Test: Habitable zone calculation ---- */
static void test_habitable_zone(void) {
    printf("Test: Habitable zone\n");
    double inner, outer;

    /* Sun-like star (L=1.0) → HZ at 0.95 - 1.37 AU */
    habitable_zone(1.0, &inner, &outer);
    ASSERT_NEAR(inner, 0.95, 0.01, "Solar HZ inner = 0.95 AU");
    ASSERT_NEAR(outer, 1.37, 0.01, "Solar HZ outer = 1.37 AU");

    /* Dim M-dwarf (L=0.01) → much closer HZ */
    habitable_zone(0.01, &inner, &outer);
    ASSERT(inner < 0.15, "M-dwarf HZ inner < 0.15 AU");
    ASSERT(outer < 0.20, "M-dwarf HZ outer < 0.20 AU");

    /* Bright A-star (L=10) → wider HZ */
    habitable_zone(10.0, &inner, &outer);
    ASSERT(inner > 2.5, "A-star HZ inner > 2.5 AU");
    ASSERT(outer > 3.5, "A-star HZ outer > 3.5 AU");
}

/* ---- Test: Planet physical plausibility ---- */
static void test_planet_physics(void) {
    printf("Test: Planet physical plausibility\n");
    system_t systems[30];
    sector_coord_t coord = {0, 0, 0};
    int count = generate_sector(systems, 30, 42, coord);

    int total_planets = 0;
    int gas_giants = 0;
    int with_water = 0;
    int habitable = 0;

    for (int i = 0; i < count; i++) {
        for (int p = 0; p < systems[i].planet_count; p++) {
            planet_t *pl = &systems[i].planets[p];
            total_planets++;

            /* Basic sanity */
            ASSERT(pl->mass_earth > 0, "Planet has positive mass");
            ASSERT(pl->radius_earth > 0, "Planet has positive radius");
            ASSERT(pl->orbital_radius_au > 0, "Planet has positive orbital radius");
            ASSERT(pl->orbital_period_days > 0, "Planet has positive orbital period");
            ASSERT(pl->eccentricity >= 0 && pl->eccentricity < 1.0, "Eccentricity in [0,1)");

            if (pl->type == PLANET_GAS_GIANT) gas_giants++;
            if (pl->water_coverage > 0.01) with_water++;
            if (pl->habitability_index > 0.5) habitable++;

            /* Gas giants should be massive */
            if (pl->type == PLANET_GAS_GIANT) {
                ASSERT(pl->mass_earth > 5.0, "Gas giant mass > 5 Earth");
            }
            /* Rocky planets shouldn't be huge */
            if (pl->type == PLANET_ROCKY) {
                ASSERT(pl->mass_earth < 3.0, "Rocky planet mass < 3 Earth");
            }
        }
    }

    printf("  Total planets: %d\n", total_planets);
    printf("  Gas giants: %d\n", gas_giants);
    printf("  With water: %d\n", with_water);
    printf("  Habitable (>0.5): %d\n", habitable);
    ASSERT(total_planets > 20, "Enough planets for testing");
    ASSERT(gas_giants > 0, "At least one gas giant");
}

/* ---- Test: Sector density varies with galactic position ---- */
static void test_sector_density(void) {
    printf("Test: Sector density variation\n");

    /* Near galactic core (0,0,0) should be denser than far halo */
    system_t systems[30];
    int core_total = 0, halo_total = 0;

    for (int i = -2; i <= 2; i++) {
        for (int j = -2; j <= 2; j++) {
            core_total += generate_sector(systems, 30, 42, (sector_coord_t){i, j, 0});
        }
    }

    for (int i = 400; i <= 404; i++) {
        for (int j = 400; j <= 404; j++) {
            halo_total += generate_sector(systems, 30, 42, (sector_coord_t){i, j, 0});
        }
    }

    printf("  Core (25 sectors): %d systems\n", core_total);
    printf("  Halo (25 sectors): %d systems\n", halo_total);
    ASSERT(core_total > halo_total, "Core denser than halo");
}

/* ---- Test: Persistence round-trip ---- */
static void test_persistence_roundtrip(void) {
    printf("Test: Persistence round-trip\n");

    const char *db_path = "/tmp/test_gen.db";
    remove(db_path);

    persist_t db;
    ASSERT(persist_open(&db, db_path) == 0, "Database opens");

    /* Generate a sector */
    system_t original[30];
    sector_coord_t coord = {3, 7, 1};
    int count = generate_sector(original, 30, 42, coord);
    ASSERT(count > 0, "Sector has systems");

    /* Save */
    ASSERT(persist_save_sector(&db, coord, 0, original, count) == 0, "Sector saves");

    /* Check exists */
    int stored = persist_sector_exists(&db, coord);
    ASSERT(stored == count, "Sector exists with correct count");

    /* Non-existent sector */
    int missing = persist_sector_exists(&db, (sector_coord_t){99, 99, 99});
    ASSERT(missing == -1, "Missing sector returns -1");

    /* Load */
    system_t loaded[30];
    int loaded_count = persist_load_sector(&db, coord, loaded, 30);
    ASSERT(loaded_count == count, "Loaded count matches");

    /* Compare each system */
    int all_match = 1;
    for (int i = 0; i < count; i++) {
        if (memcmp(&original[i], &loaded[i], sizeof(system_t)) != 0) {
            all_match = 0;
            printf("  System %d differs!\n", i);
        }
    }
    ASSERT(all_match, "All systems byte-identical after round-trip");

    persist_close(&db);
    remove(db_path);
}

/* ---- Test: Generation speed ---- */
static void test_generation_speed(void) {
    printf("Test: Generation speed\n");
    struct timespec t0, t1;

    system_t systems[30];
    int total_systems = 0;
    int total_sectors = 100;

    clock_gettime(CLOCK_MONOTONIC, &t0);
    for (int i = 0; i < total_sectors; i++) {
        sector_coord_t coord = {i % 10, i / 10, 0};
        total_systems += generate_sector(systems, 30, 42, coord);
    }
    clock_gettime(CLOCK_MONOTONIC, &t1);

    double elapsed = (t1.tv_sec - t0.tv_sec) + (t1.tv_nsec - t0.tv_nsec) / 1e9;
    printf("  %d sectors, %d systems in %.3f ms (%.0f sectors/sec)\n",
        total_sectors, total_systems, elapsed * 1000.0,
        total_sectors / elapsed);
    ASSERT(elapsed < 1.0, "100 sectors in under 1 second");
}

/* ---- Main ---- */
int main(void) {
    printf("=== Phase 1: Generation Tests ===\n\n");

    test_rng_determinism();
    printf("\n");
    test_sector_determinism();
    printf("\n");
    test_star_distribution();
    printf("\n");
    test_habitable_zone();
    printf("\n");
    test_planet_physics();
    printf("\n");
    test_sector_density();
    printf("\n");
    test_persistence_roundtrip();
    printf("\n");
    test_generation_speed();

    printf("\n=== Results: %d passed, %d failed ===\n",
        tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
