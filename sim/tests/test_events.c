/*
 * test_events.c — Phase 9: Events & Encounters tests
 *
 * Tests: event generation, hazards, anomalies, alien life,
 *        personality impact, determinism, frequencies.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/events.h"
#include "../src/generate.h"

static int passed = 0, failed = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { passed++; } \
    else { failed++; printf("  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); } \
} while(0)

#define ASSERT_EQ_INT(a, b, msg) do { \
    int _a = (a), _b = (b); \
    if (_a == _b) { passed++; } \
    else { failed++; printf("  FAIL [%s:%d]: %s (got %d, expected %d)\n", __FILE__, __LINE__, msg, _a, _b); } \
} while(0)

#define ASSERT_NEAR(a, b, eps, msg) do { \
    double _a = (a), _b = (b); \
    if (fabs(_a - _b) < (eps)) { passed++; } \
    else { failed++; printf("  FAIL [%s:%d]: %s (got %.6f, expected %.6f)\n", __FILE__, __LINE__, msg, _a, _b); } \
} while(0)

/* Helper: make a probe in a system */
static probe_t make_probe_in_system(uint64_t id_lo) {
    probe_t p = {0};
    p.id = (probe_uid_t){0, id_lo};
    p.location_type = LOC_IN_SYSTEM;
    p.status = STATUS_ACTIVE;
    p.hull_integrity = 1.0f;
    p.energy_joules = 1000000.0;
    p.tech_levels[TECH_MATERIALS] = 3;
    p.tech_levels[TECH_SENSORS] = 3;
    p.tech_levels[TECH_COMPUTING] = 3;
    p.personality.curiosity = 0.5f;
    p.personality.caution = 0.0f;
    p.personality.empathy = 0.0f;
    p.personality.existential_angst = 0.0f;
    p.personality.nostalgia_for_earth = 0.0f;
    p.personality.drift_rate = 1.0f;
    return p;
}

/* Helper: make a basic star system */
static system_t make_system(uint64_t id_lo) {
    system_t s = {0};
    s.id = (probe_uid_t){0, id_lo};
    s.star_count = 1;
    s.stars[0].class = STAR_G;
    s.planet_count = 3;
    s.planets[0].type = PLANET_ROCKY;
    s.planets[0].habitability_index = 0.7;
    s.planets[0].id = (probe_uid_t){0, id_lo * 100 + 1};
    s.planets[1].type = PLANET_GAS_GIANT;
    s.planets[1].habitability_index = 0.0;
    s.planets[1].id = (probe_uid_t){0, id_lo * 100 + 2};
    s.planets[2].type = PLANET_ICE;
    s.planets[2].habitability_index = 0.1;
    s.planets[2].id = (probe_uid_t){0, id_lo * 100 + 3};
    return s;
}

/* ================================================
 * Test 1: Event system initialization
 * ================================================ */
static void test_init(void) {
    printf("Test: Event system initializes clean\n");

    event_system_t es;
    events_init(&es);

    ASSERT_EQ_INT(es.count, 0, "no events");
    ASSERT_EQ_INT(es.anomaly_count, 0, "no anomalies");
    ASSERT_EQ_INT(es.civ_count, 0, "no civilizations");
}

/* ================================================
 * Test 2: Generate discovery event
 * ================================================ */
static void test_generate_discovery(void) {
    printf("Test: Generate discovery event\n");

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 42);

    probe_t probe = make_probe_in_system(1);
    system_t sys = make_system(100);

    int ret = events_generate(&es, &probe, EVT_DISCOVERY, DISC_MINERAL_DEPOSIT,
                              &sys, 1000, &rng);
    ASSERT_EQ_INT(ret, 0, "generate succeeds");
    ASSERT_EQ_INT(es.count, 1, "one event logged");
    ASSERT_EQ_INT((int)es.events[0].type, EVT_DISCOVERY, "type is discovery");
    ASSERT_EQ_INT((int)es.events[0].subtype, DISC_MINERAL_DEPOSIT, "subtype mineral");
    ASSERT(uid_eq(es.events[0].probe_id, probe.id), "probe id matches");
    ASSERT(es.events[0].description[0] != '\0', "has description");
    ASSERT(es.events[0].severity > 0.0f, "has severity");
}

/* ================================================
 * Test 3: Hazard — solar flare damages hull
 * ================================================ */
static void test_hazard_solar_flare(void) {
    printf("Test: Solar flare damages hull\n");

    probe_t probe = make_probe_in_system(1);
    probe.hull_integrity = 1.0f;
    probe.tech_levels[TECH_MATERIALS] = 1;  /* low shielding */

    float damage = hazard_solar_flare(&probe, 0.8f);
    ASSERT(damage > 0.0f, "flare deals damage");
    ASSERT(probe.hull_integrity < 1.0f, "hull reduced");
    ASSERT(probe.hull_integrity > 0.0f, "probe not destroyed by single flare");

    /* Higher materials tech = less damage */
    probe_t shielded = make_probe_in_system(2);
    shielded.hull_integrity = 1.0f;
    shielded.tech_levels[TECH_MATERIALS] = 8;  /* high shielding */

    float damage2 = hazard_solar_flare(&shielded, 0.8f);
    ASSERT(damage2 < damage, "higher materials tech = less damage");
}

/* ================================================
 * Test 4: Hazard — asteroid collision
 * ================================================ */
static void test_hazard_asteroid(void) {
    printf("Test: Asteroid collision damages hull\n");

    probe_t probe = make_probe_in_system(1);
    probe.hull_integrity = 1.0f;

    float damage = hazard_asteroid(&probe, 0.5f);
    ASSERT(damage > 0.0f, "asteroid deals damage");
    ASSERT(probe.hull_integrity < 1.0f, "hull reduced after asteroid");
}

/* ================================================
 * Test 5: Hazard — radiation burst
 * ================================================ */
static void test_hazard_radiation(void) {
    printf("Test: Radiation burst damages computing\n");

    probe_t probe = make_probe_in_system(1);
    probe.compute_capacity = 1.0f;

    float damage = hazard_radiation(&probe, 0.6f);
    ASSERT(damage > 0.0f, "radiation deals damage");
    ASSERT(probe.compute_capacity < 1.0f, "compute capacity reduced");
}

/* ================================================
 * Test 6: Anomaly generates persistent marker
 * ================================================ */
static void test_anomaly_marker(void) {
    printf("Test: Anomaly creates persistent marker\n");

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 99);

    probe_t probe = make_probe_in_system(1);
    system_t sys = make_system(100);

    events_generate(&es, &probe, EVT_ANOMALY, ANOM_UNEXPLAINED_SIGNAL,
                    &sys, 2000, &rng);

    ASSERT_EQ_INT(es.anomaly_count, 1, "one anomaly recorded");
    ASSERT(uid_eq(es.anomalies[0].system_id, sys.id), "anomaly in correct system");
    ASSERT(!es.anomalies[0].resolved, "anomaly not yet resolved");

    /* Query anomalies in system */
    anomaly_t found[10];
    int count = events_get_anomalies(&es, sys.id, found, 10);
    ASSERT_EQ_INT(count, 1, "found 1 anomaly in system");
}

/* ================================================
 * Test 7: Alien life check — deterministic per planet
 * ================================================ */
static void test_alien_check(void) {
    printf("Test: Alien life check deterministic per planet\n");

    /* Create a planet with high habitability */
    planet_t planet = {0};
    planet.id = (probe_uid_t){0, 777};
    planet.type = PLANET_ROCKY;
    planet.habitability_index = 0.9;
    planet.water_coverage = 0.6;

    /* Run check multiple times with same rng seed — should give same result */
    rng_t rng1, rng2;
    rng_seed(&rng1, 12345);
    rng_seed(&rng2, 12345);

    int result1 = alien_check_planet(&planet, &rng1);
    int result2 = alien_check_planet(&planet, &rng2);
    ASSERT_EQ_INT(result1, result2, "same seed = same result");
}

/* ================================================
 * Test 8: Alien civilization generation
 * ================================================ */
static void test_alien_civ_generation(void) {
    printf("Test: Alien civilization generated correctly\n");

    rng_t rng;
    rng_seed(&rng, 54321);

    planet_t planet = {0};
    planet.id = (probe_uid_t){0, 888};
    planet.type = PLANET_ROCKY;
    planet.habitability_index = 0.85;
    planet.water_coverage = 0.5;

    civilization_t civ = {0};
    /* Force a generation by trying many seeds until we get life */
    int got_life = 0;
    for (uint64_t seed = 1; seed < 100000 && !got_life; seed++) {
        rng_seed(&rng, seed);
        if (alien_generate_civ(&civ, &planet, (probe_uid_t){0, 1}, 5000, &rng) == 0) {
            got_life = 1;
        }
    }
    ASSERT(got_life, "found a seed that generates life");

    if (got_life) {
        ASSERT(civ.name[0] != '\0', "civ has name");
        ASSERT((int)civ.type >= 0 && (int)civ.type < CIV_TYPE_COUNT, "valid civ type");
        ASSERT((int)civ.disposition >= 0 && (int)civ.disposition < DISP_COUNT, "valid disposition");
        ASSERT(civ.tech_level <= 20, "tech level in range");
        ASSERT((int)civ.biology_base >= 0 && (int)civ.biology_base < BIO_BASE_COUNT, "valid biology");
        ASSERT((int)civ.state >= 0 && (int)civ.state < CIV_STATE_COUNT, "valid state");
        ASSERT(uid_eq(civ.homeworld_id, planet.id), "homeworld set");
    }
}

/* ================================================
 * Test 9: Extinct civilization has artifacts
 * ================================================ */
static void test_extinct_civ_artifacts(void) {
    printf("Test: Extinct civilization has artifacts\n");

    rng_t rng;
    planet_t planet = {0};
    planet.id = (probe_uid_t){0, 999};
    planet.type = PLANET_ROCKY;
    planet.habitability_index = 0.8;

    civilization_t civ = {0};
    int got_extinct = 0;
    for (uint64_t seed = 1; seed < 200000 && !got_extinct; seed++) {
        rng_seed(&rng, seed);
        if (alien_generate_civ(&civ, &planet, (probe_uid_t){0,1}, 5000, &rng) == 0) {
            if (civ.type == CIV_EXTINCT || civ.state == CIV_STATE_EXTINCT) {
                got_extinct = 1;
            }
        }
    }
    ASSERT(got_extinct, "found extinct civilization");
    if (got_extinct) {
        ASSERT(civ.artifact_count > 0, "extinct civ has artifacts");
        ASSERT(civ.artifacts[0][0] != '\0', "artifact has description");
    }
}

/* ================================================
 * Test 10: Discovery event affects personality (curiosity)
 * ================================================ */
static void test_discovery_personality(void) {
    printf("Test: Discovery event affects curiosity\n");

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 42);

    probe_t probe = make_probe_in_system(1);
    float old_curiosity = probe.personality.curiosity;
    system_t sys = make_system(100);

    events_generate(&es, &probe, EVT_DISCOVERY, DISC_GEOLOGICAL_FORMATION,
                    &sys, 1000, &rng);

    /* Discovery should have triggered personality drift */
    ASSERT(probe.personality.curiosity != old_curiosity,
           "curiosity changed after discovery");
}

/* ================================================
 * Test 11: Wonder event affects personality (nostalgia/angst)
 * ================================================ */
static void test_wonder_personality(void) {
    printf("Test: Wonder event affects personality\n");

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 77);

    probe_t probe = make_probe_in_system(1);
    float old_nostalgia = probe.personality.nostalgia_for_earth;
    float old_angst = probe.personality.existential_angst;
    system_t sys = make_system(100);

    events_generate(&es, &probe, EVT_WONDER, WONDER_BINARY_SUNSET,
                    &sys, 3000, &rng);

    /* Wonder should move nostalgia or angst */
    bool changed = (probe.personality.nostalgia_for_earth != old_nostalgia) ||
                   (probe.personality.existential_angst != old_angst);
    ASSERT(changed, "wonder affected nostalgia or angst");
}

/* ================================================
 * Test 12: Encounter event with alien life → empathy/curiosity
 * ================================================ */
static void test_encounter_personality(void) {
    printf("Test: Encounter with alien life affects empathy and curiosity\n");

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 88);

    probe_t probe = make_probe_in_system(1);
    probe.personality.empathy = 0.0f;
    probe.personality.curiosity = 0.0f;
    system_t sys = make_system(100);

    events_generate(&es, &probe, EVT_ENCOUNTER, 0, &sys, 4000, &rng);

    /* Encounter should affect empathy or curiosity */
    bool changed = (probe.personality.empathy != 0.0f) ||
                   (probe.personality.curiosity != 0.0f);
    ASSERT(changed, "encounter affected empathy or curiosity");
}

/* ================================================
 * Test 13: Events are deterministic for same seed
 * ================================================ */
static void test_determinism(void) {
    printf("Test: Events deterministic for given seed\n");

    event_type_t types1[100], types2[100];
    int count1 = 0, count2 = 0;

    bool ok1 = events_deterministic_check(42, 10000, types1, &count1, 100);
    bool ok2 = events_deterministic_check(42, 10000, types2, &count2, 100);

    ASSERT(ok1, "run 1 succeeded");
    ASSERT(ok2, "run 2 succeeded");
    ASSERT_EQ_INT(count1, count2, "same number of events");

    bool all_match = true;
    for (int i = 0; i < count1 && i < count2; i++) {
        if (types1[i] != types2[i]) { all_match = false; break; }
    }
    ASSERT(all_match, "event types match between runs");
}

/* ================================================
 * Test 14: Run many ticks — events fire with rough distribution
 * ================================================ */
static void test_frequency_distribution(void) {
    printf("Test: Event frequency distribution over 100k ticks\n");

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 12345);

    probe_t probe = make_probe_in_system(1);
    system_t sys = make_system(100);

    int counts[EVT_TYPE_COUNT] = {0};

    for (uint64_t t = 0; t < 100000; t++) {
        /* Reset probe to keep it alive */
        probe.hull_integrity = 1.0f;
        probe.compute_capacity = 1.0f;
        probe.energy_joules = 1000000.0;

        int before = es.count;
        events_tick_probe(&es, &probe, &sys, t, &rng);

        for (int i = before; i < es.count && i < MAX_EVENT_LOG; i++) {
            int et = (int)es.events[i].type;
            if (et >= 0 && et < EVT_TYPE_COUNT) counts[et]++;
        }
    }

    /* Check that at least some of each common type fired */
    ASSERT(counts[EVT_DISCOVERY] > 100, "discoveries fired (>100)");
    ASSERT(counts[EVT_HAZARD] > 50, "hazards fired (>50)");
    ASSERT(counts[EVT_ANOMALY] > 20, "anomalies fired (>20)");
    ASSERT(counts[EVT_WONDER] > 5, "wonders fired (>5)");

    /* Discovery should be the most frequent */
    ASSERT(counts[EVT_DISCOVERY] > counts[EVT_ANOMALY],
           "discovery more frequent than anomaly");
    ASSERT(counts[EVT_DISCOVERY] > counts[EVT_WONDER],
           "discovery more frequent than wonder");

    /* Crisis should be very rare */
    ASSERT(counts[EVT_CRISIS] < counts[EVT_DISCOVERY],
           "crisis rarer than discovery");
}

/* ================================================
 * Test 15: Hazard event via events_generate applies damage
 * ================================================ */
static void test_hazard_via_generate(void) {
    printf("Test: Hazard generated via events_generate deals damage\n");

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 55);

    probe_t probe = make_probe_in_system(1);
    probe.hull_integrity = 1.0f;
    system_t sys = make_system(100);

    events_generate(&es, &probe, EVT_HAZARD, HAZ_SOLAR_FLARE,
                    &sys, 5000, &rng);

    ASSERT(probe.hull_integrity < 1.0f, "hull damaged by generated hazard");
    ASSERT_EQ_INT(es.count, 1, "event logged");
}

/* ================================================
 * Test 16: Query events for specific probe
 * ================================================ */
static void test_query_probe_events(void) {
    printf("Test: Query events for specific probe\n");

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 66);

    probe_t p1 = make_probe_in_system(1);
    probe_t p2 = make_probe_in_system(2);
    system_t sys = make_system(100);

    events_generate(&es, &p1, EVT_DISCOVERY, DISC_MINERAL_DEPOSIT, &sys, 100, &rng);
    events_generate(&es, &p1, EVT_WONDER, WONDER_BINARY_SUNSET, &sys, 200, &rng);
    events_generate(&es, &p2, EVT_DISCOVERY, DISC_UNDERGROUND_WATER, &sys, 300, &rng);

    sim_event_t found[10];
    int count = events_get_for_probe(&es, p1.id, found, 10);
    ASSERT_EQ_INT(count, 2, "probe 1 has 2 events");

    count = events_get_for_probe(&es, p2.id, found, 10);
    ASSERT_EQ_INT(count, 1, "probe 2 has 1 event");
}

/* ================================================
 * Test 17: Get civilization on planet
 * ================================================ */
static void test_get_civ(void) {
    printf("Test: Query civilization on planet\n");

    event_system_t es;
    events_init(&es);
    rng_t rng;

    planet_t planet = {0};
    planet.id = (probe_uid_t){0, 555};
    planet.type = PLANET_ROCKY;
    planet.habitability_index = 0.85;

    /* Generate a civ and register it */
    civilization_t civ = {0};
    int got = 0;
    for (uint64_t seed = 1; seed < 200000 && !got; seed++) {
        rng_seed(&rng, seed);
        if (alien_generate_civ(&civ, &planet, (probe_uid_t){0,1}, 5000, &rng) == 0) {
            got = 1;
        }
    }
    ASSERT(got, "generated a civ");

    if (got) {
        /* Manually add to event system */
        es.civilizations[es.civ_count++] = civ;

        const civilization_t *found = events_get_civ(&es, planet.id);
        ASSERT(found != NULL, "found civ on planet");
        if (found) {
            ASSERT(uid_eq(found->homeworld_id, planet.id), "correct planet");
        }

        /* Query non-existent planet */
        const civilization_t *none = events_get_civ(&es, (probe_uid_t){0, 9999});
        ASSERT(none == NULL, "no civ on random planet");
    }
}

/* ================================================
 * Test 18: Crisis event is severe
 * ================================================ */
static void test_crisis_event(void) {
    printf("Test: Crisis event has high severity\n");

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 111);

    probe_t probe = make_probe_in_system(1);
    system_t sys = make_system(100);

    events_generate(&es, &probe, EVT_CRISIS, CRISIS_SYSTEM_FAILURE,
                    &sys, 7000, &rng);

    ASSERT_EQ_INT(es.count, 1, "crisis logged");
    ASSERT(es.events[0].severity >= 0.5f, "crisis is high severity");
}

/* ================================================
 * Test 19: Memory recorded for events
 * ================================================ */
static void test_event_records_memory(void) {
    printf("Test: Events record episodic memory\n");

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 222);

    probe_t probe = make_probe_in_system(1);
    probe.memory_count = 0;
    system_t sys = make_system(100);

    events_generate(&es, &probe, EVT_WONDER, WONDER_SUPERNOVA_VISIBLE,
                    &sys, 8000, &rng);

    ASSERT(probe.memory_count > 0, "memory recorded after wonder event");
    if (probe.memory_count > 0) {
        ASSERT(probe.memories[0].emotional_weight > 0.0f,
               "memory has emotional weight");
    }
}

/* ================================================
 * Entry point
 * ================================================ */
int main(void) {
    printf("=== Phase 9: Events & Encounters Tests ===\n\n");

    test_init();
    test_generate_discovery();
    test_hazard_solar_flare();
    test_hazard_asteroid();
    test_hazard_radiation();
    test_anomaly_marker();
    test_alien_check();
    test_alien_civ_generation();
    test_extinct_civ_artifacts();
    test_discovery_personality();
    test_wonder_personality();
    test_encounter_personality();
    test_determinism();
    test_frequency_distribution();
    test_hazard_via_generate();
    test_query_probe_events();
    test_get_civ();
    test_crisis_event();
    test_event_records_memory();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
