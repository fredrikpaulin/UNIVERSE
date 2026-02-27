/*
 * test_personality.c — Phase 6: Personality, memory, monologue, quirks
 *
 * Tests:
 *   - Personality drift from discovery (curiosity up)
 *   - Personality drift from damage (caution up)
 *   - Solitude drift after 1000+ ticks
 *   - Trait clamping to [-1, 1]
 *   - Memory recording and fading
 *   - Memory eviction when full
 *   - Opinion formation after survey
 *   - Monologue varies with personality
 *   - Quirk: food naming when stressed
 *   - Trait get/set by index
 */

#include "universe.h"
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

/* ---- Test: Discovery increases curiosity ---- */

static void test_drift_discovery(void) {
    printf("Test: Discovery drift → curiosity up\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);

    float before = probe.personality.curiosity;
    personality_drift(&probe, DRIFT_DISCOVERY);
    float after = probe.personality.curiosity;

    ASSERT(after > before, "Curiosity increased after discovery");
    ASSERT(after <= 1.0f, "Curiosity still <= 1.0");

    /* Ambition should also nudge up slightly */
    /* (discovering things feeds ambition) */
}

/* ---- Test: Damage increases caution ---- */

static void test_drift_damage(void) {
    printf("Test: Damage drift → caution up\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);

    float before_caution = probe.personality.caution;
    float before_angst = probe.personality.existential_angst;
    personality_drift(&probe, DRIFT_DAMAGE);

    ASSERT(probe.personality.caution > before_caution,
           "Caution increased after damage");
    ASSERT(probe.personality.existential_angst >= before_angst,
           "Existential angst doesn't decrease from damage");
}

/* ---- Test: Solitude drift after many ticks ---- */

static void test_solitude_drift(void) {
    printf("Test: Solitude drift after 1000+ ticks\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);

    float initial_sociability = probe.personality.sociability;

    /* Simulate 1500 solo ticks */
    for (uint64_t t = 1; t <= 1500; t++) {
        personality_tick_solitude(&probe, t);
    }

    float final_sociability = probe.personality.sociability;
    float diff = fabsf(final_sociability - initial_sociability);

    ASSERT(diff > 0.01f, "Sociability shifted measurably after 1500 solo ticks");

    /* Also check nostalgia drifts during solitude */
    /* (lonely probes think about Earth more) */
}

/* ---- Test: Trait clamping ---- */

static void test_trait_clamping(void) {
    printf("Test: Traits clamped to [-1, 1]\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);

    /* Push curiosity way up */
    probe.personality.curiosity = 0.95f;
    probe.personality.drift_rate = 1.0f; /* high drift for fast testing */
    for (int i = 0; i < 100; i++) {
        personality_drift(&probe, DRIFT_DISCOVERY);
    }
    ASSERT(probe.personality.curiosity <= 1.0f, "Curiosity clamped at 1.0");
    ASSERT(probe.personality.curiosity >= -1.0f, "Curiosity >= -1.0");

    /* Push caution way up */
    probe.personality.caution = 0.95f;
    for (int i = 0; i < 100; i++) {
        personality_drift(&probe, DRIFT_DAMAGE);
    }
    ASSERT(probe.personality.caution <= 1.0f, "Caution clamped at 1.0");

    /* Check all traits are in range */
    float *traits = (float *)&probe.personality;
    for (int i = 0; i < TRAIT_COUNT; i++) {
        ASSERT(traits[i] >= -1.0f && traits[i] <= 1.0f,
               "All traits in [-1, 1]");
    }
}

/* ---- Test: Memory recording and fading ---- */

static void test_memory_basic(void) {
    printf("Test: Memory recording and fading\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));

    /* Record some memories */
    memory_record(&probe, 100, "Found a strange signal", 0.8f);
    ASSERT(probe.memory_count == 1, "1 memory recorded");

    memory_record(&probe, 200, "Mined iron on planet Alpha-3", 0.3f);
    ASSERT(probe.memory_count == 2, "2 memories recorded");

    /* Check first memory */
    ASSERT(probe.memories[0].tick == 100, "Memory 0 tick = 100");
    ASSERT_NEAR(probe.memories[0].emotional_weight, 0.8f, 0.01f,
                "Memory 0 weight = 0.8");
    ASSERT_NEAR(probe.memories[0].fading, 0.0f, 0.01f,
                "Memory 0 starts vivid");

    /* Fade memories */
    for (int i = 0; i < 1000; i++) {
        memory_fade_tick(&probe);
    }

    ASSERT(probe.memories[0].fading > 0.0f, "Memory faded after 1000 ticks");
    ASSERT(probe.memories[0].fading < 1.0f, "Memory not fully forgotten");

    /* Memory at tick 100 has more fading than memory at tick 200 */
    /* (both faded same amount since recording, but let's test the value) */
    float fading_0 = probe.memories[0].fading;

    /* Record a fresh memory and fade more */
    memory_record(&probe, 1200, "Fresh memory", 0.5f);
    for (int i = 0; i < 100; i++) {
        memory_fade_tick(&probe);
    }

    ASSERT(probe.memories[0].fading > fading_0,
           "Old memory faded further after more ticks");
    ASSERT(probe.memories[2].fading < probe.memories[0].fading,
           "Newer memory less faded than older");
}

/* ---- Test: Memory eviction when full ---- */

static void test_memory_eviction(void) {
    printf("Test: Memory eviction when full\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));

    /* Fill all memory slots */
    for (int i = 0; i < MAX_MEMORIES; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "Event %d", i);
        memory_record(&probe, (uint64_t)i * 10, buf, 0.5f);
    }
    ASSERT(probe.memory_count == MAX_MEMORIES, "Memory full");

    /* Heavily fade the first few */
    probe.memories[0].fading = 0.99f;
    probe.memories[1].fading = 0.98f;

    /* Record one more — should evict the most faded */
    memory_record(&probe, 9999, "New important event", 0.9f);
    ASSERT(probe.memory_count == MAX_MEMORIES, "Still at max");

    /* The most faded one (0.99) should be gone */
    bool found_new = false;
    bool found_old = false;
    for (int i = 0; i < probe.memory_count; i++) {
        if (probe.memories[i].tick == 9999) found_new = true;
        if (probe.memories[i].fading >= 0.99f) found_old = true;
    }
    ASSERT(found_new, "New memory was added");
    ASSERT(!found_old, "Most faded memory was evicted");
}

/* ---- Test: Opinion formation ---- */

static void test_opinion_formation(void) {
    printf("Test: Opinion formation after survey\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);

    /* Create a system with good mining potential */
    system_t sys;
    memset(&sys, 0, sizeof(sys));
    snprintf(sys.name, MAX_NAME, "Alpha Centauri");
    sys.planet_count = 2;
    sys.planets[0].resources[RES_IRON] = 0.9f;
    sys.planets[0].resources[RES_SILICON] = 0.7f;
    sys.planets[0].type = PLANET_ROCKY;
    snprintf(sys.planets[0].name, MAX_NAME, "Alpha Centauri I");
    sys.planets[1].type = PLANET_GAS_GIANT;
    sys.planets[1].habitability_index = 0.0f;
    snprintf(sys.planets[1].name, MAX_NAME, "Alpha Centauri II");

    uint16_t before = probe.memory_count;
    opinion_form_system(&probe, &sys, 500);
    ASSERT(probe.memory_count > before, "Opinion stored as memory");

    /* Check the opinion mentions mining or resources */
    bool mentions_mining = false;
    for (int i = 0; i < probe.memory_count; i++) {
        if (strstr(probe.memories[i].event, "mining") ||
            strstr(probe.memories[i].event, "resource") ||
            strstr(probe.memories[i].event, "Mining") ||
            strstr(probe.memories[i].event, "Resource") ||
            strstr(probe.memories[i].event, "rich")) {
            mentions_mining = true;
            break;
        }
    }
    ASSERT(mentions_mining, "Opinion mentions mining/resources for resource-rich system");

    /* Now a boring system */
    system_t boring;
    memset(&boring, 0, sizeof(boring));
    snprintf(boring.name, MAX_NAME, "Dullsville");
    boring.planet_count = 1;
    boring.planets[0].type = PLANET_ROCKY;
    boring.planets[0].habitability_index = 0.0f;
    /* All resources near zero (already 0 from memset) */

    before = probe.memory_count;
    opinion_form_system(&probe, &boring, 600);
    ASSERT(probe.memory_count > before, "Opinion stored for boring system too");
}

/* ---- Test: Monologue varies with personality ---- */

static void test_monologue(void) {
    printf("Test: Monologue varies with personality\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);

    char buf1[256], buf2[256];

    /* High humor probe */
    probe.personality.humor = 0.9f;
    probe.personality.caution = 0.1f;
    monologue_generate(buf1, sizeof(buf1), &probe, DRIFT_DISCOVERY);
    ASSERT(strlen(buf1) > 0, "Monologue generated for high-humor probe");

    /* High caution, low humor probe */
    probe.personality.humor = 0.1f;
    probe.personality.caution = 0.9f;
    monologue_generate(buf2, sizeof(buf2), &probe, DRIFT_DAMAGE);
    ASSERT(strlen(buf2) > 0, "Monologue generated for high-caution probe");

    /* They should be different */
    ASSERT(strcmp(buf1, buf2) != 0, "Different personalities → different monologue");

    /* Test all event types produce something */
    for (int e = 0; e < DRIFT_TYPE_COUNT; e++) {
        char tmp[256];
        monologue_generate(tmp, sizeof(tmp), &probe, (drift_event_t)e);
        ASSERT(strlen(tmp) > 0, "Monologue non-empty for event type");
    }
}

/* ---- Test: Quirk — food naming when stressed ---- */

static void test_quirk_food_naming(void) {
    printf("Test: Quirk — food naming when stressed\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);

    system_t sys;
    memset(&sys, 0, sizeof(sys));
    snprintf(sys.name, MAX_NAME, "HD 219134");

    /* Hull above 0.5 — quirk should NOT fire */
    probe.hull_integrity = 0.8f;
    bool fired = quirk_check_naming(&probe, &sys);
    ASSERT(!fired, "Quirk doesn't fire when hull > 0.5");
    ASSERT(strcmp(sys.name, "HD 219134") == 0, "Name unchanged");

    /* Hull below 0.5 — quirk SHOULD fire */
    probe.hull_integrity = 0.3f;
    fired = quirk_check_naming(&probe, &sys);
    ASSERT(fired, "Quirk fires when hull < 0.5");
    ASSERT(strcmp(sys.name, "HD 219134") != 0, "Name changed to food");

    /* The name should be a recognizable food word */
    /* (We'll just check it's non-empty and different) */
    ASSERT(strlen(sys.name) > 0, "Food name is non-empty");

    /* Without the quirk, it shouldn't fire */
    probe_t probe2;
    memset(&probe2, 0, sizeof(probe2));
    probe2.hull_integrity = 0.3f;
    probe2.quirk_count = 0; /* no quirks */

    system_t sys2;
    memset(&sys2, 0, sizeof(sys2));
    snprintf(sys2.name, MAX_NAME, "Test System");

    fired = quirk_check_naming(&probe2, &sys2);
    ASSERT(!fired, "No quirk → no rename");
}

/* ---- Test: Trait get/set by index ---- */

static void test_trait_accessors(void) {
    printf("Test: Trait get/set by index\n");

    personality_traits_t p = {0};
    p.curiosity = 0.5f;
    p.caution = 0.3f;

    ASSERT_NEAR(trait_get(&p, 0), 0.5f, 0.001f, "Index 0 = curiosity");
    ASSERT_NEAR(trait_get(&p, 1), 0.3f, 0.001f, "Index 1 = caution");

    trait_set(&p, 0, 0.9f);
    ASSERT_NEAR(p.curiosity, 0.9f, 0.001f, "Set curiosity to 0.9");

    /* Test clamping */
    trait_set(&p, 0, 1.5f);
    ASSERT_NEAR(p.curiosity, 1.0f, 0.001f, "Clamped to 1.0");

    trait_set(&p, 0, -1.5f);
    ASSERT_NEAR(p.curiosity, -1.0f, 0.001f, "Clamped to -1.0");

    /* All 10 traits accessible */
    for (int i = 0; i < TRAIT_COUNT; i++) {
        trait_set(&p, i, 0.42f);
        ASSERT_NEAR(trait_get(&p, i), 0.42f, 0.001f, "Round-trip trait by index");
    }
}

/* ---- Test: Beautiful system triggers curiosity + nostalgia ---- */

static void test_drift_beautiful(void) {
    printf("Test: Beautiful system drift\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);

    float before_curiosity = probe.personality.curiosity;
    float before_nostalgia = probe.personality.nostalgia_for_earth;

    personality_drift(&probe, DRIFT_BEAUTIFUL_SYSTEM);

    ASSERT(probe.personality.curiosity > before_curiosity,
           "Curiosity up after beautiful system");
    ASSERT(probe.personality.nostalgia_for_earth > before_nostalgia,
           "Nostalgia up after beautiful system");
}

/* ---- Test: Dead civilization drift ---- */

static void test_drift_dead_civ(void) {
    printf("Test: Dead civilization drift\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));
    probe_init_bob(&probe);

    float before_angst = probe.personality.existential_angst;
    float before_nostalgia = probe.personality.nostalgia_for_earth;

    personality_drift(&probe, DRIFT_DEAD_CIVILIZATION);

    ASSERT(probe.personality.existential_angst > before_angst,
           "Existential angst up after dead civ");
    ASSERT(probe.personality.nostalgia_for_earth > before_nostalgia,
           "Nostalgia up after dead civ");
}

/* ---- Test: Most vivid memory ---- */

static void test_memory_most_vivid(void) {
    printf("Test: Most vivid memory lookup\n");

    probe_t probe;
    memset(&probe, 0, sizeof(probe));

    ASSERT(memory_most_vivid(&probe) == NULL, "No memories → NULL");

    memory_record(&probe, 10, "First event", 0.5f);
    memory_record(&probe, 20, "Second event", 0.9f);
    memory_record(&probe, 30, "Third event", 0.3f);

    /* Fade the first one a lot */
    probe.memories[0].fading = 0.8f;

    const memory_t *vivid = memory_most_vivid(&probe);
    ASSERT(vivid != NULL, "Found vivid memory");
    if (vivid) {
        ASSERT(vivid->tick == 20 || vivid->tick == 30,
               "Most vivid is one of the less-faded ones");
    }
}

/* ---- Main ---- */

int main(void) {
    printf("=== Phase 6: Personality Tests ===\n\n");

    test_drift_discovery();
    test_drift_damage();
    test_solitude_drift();
    test_trait_clamping();
    test_memory_basic();
    test_memory_eviction();
    test_opinion_formation();
    test_monologue();
    test_quirk_food_naming();
    test_trait_accessors();
    test_drift_beautiful();
    test_drift_dead_civ();
    test_memory_most_vivid();

    printf("\n=== Results: %d passed, %d failed ===\n", g_pass, g_fail);
    return g_fail > 0 ? 1 : 0;
}
