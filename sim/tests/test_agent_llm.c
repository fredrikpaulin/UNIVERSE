/*
 * test_agent_llm.c — Phase 11: LLM Agent Integration tests
 *
 * Tests: prompt building, response parsing, context management,
 *        cost tracking, deliberation throttling, decision logging,
 *        personality-driven prompt flavor.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/agent_llm.h"

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

/* Helper: make a probe with personality */
static probe_t make_llm_probe(void) {
    probe_t p = {0};
    p.id = (probe_uid_t){0, 1};
    snprintf(p.name, MAX_NAME, "Bob");
    p.status = STATUS_ACTIVE;
    p.location_type = LOC_IN_SYSTEM;
    p.hull_integrity = 0.85f;
    p.energy_joules = 500000.0;
    p.fuel_kg = 10000.0;
    p.mass_kg = 50000.0;
    p.speed_c = 0.0;
    p.tech_levels[TECH_PROPULSION] = 3;
    p.tech_levels[TECH_SENSORS] = 4;
    p.tech_levels[TECH_MINING] = 2;
    p.personality.curiosity = 0.8f;
    p.personality.caution = -0.3f;
    p.personality.sociability = 0.2f;
    p.personality.humor = 0.6f;
    p.personality.empathy = 0.4f;
    p.personality.ambition = 0.7f;
    p.personality.creativity = 0.5f;
    p.personality.stubbornness = 0.3f;
    p.personality.existential_angst = 0.1f;
    p.personality.nostalgia_for_earth = 0.4f;
    p.personality.drift_rate = 1.0f;

    /* Add quirks */
    snprintf(p.quirks[0], MAX_QUIRK_LEN, "Names systems after pizza toppings when stressed");
    snprintf(p.quirks[1], MAX_QUIRK_LEN, "Hums classical music while mining");
    p.quirk_count = 2;

    /* Earth memories */
    snprintf(p.earth_memories[0], MAX_EARTH_MEM_LEN, "The smell of coffee in the morning");
    snprintf(p.earth_memories[1], MAX_EARTH_MEM_LEN, "Watching Star Trek reruns");
    p.earth_memory_count = 2;
    p.earth_memory_fidelity = 0.9f;

    /* A recent memory */
    p.memories[0] = (memory_t){
        .tick = 100, .emotional_weight = 0.7f, .fading = 0.1f
    };
    snprintf(p.memories[0].event, 256, "Discovered a habitable planet in the Tau Ceti system");
    p.memory_count = 1;

    /* A relationship */
    p.relationships[0].other_id = (probe_uid_t){0, 2};
    p.relationships[0].trust = 0.6f;
    p.relationships[0].disposition = 1; /* friendly */
    p.relationship_count = 1;

    return p;
}

static system_t make_test_system(void) {
    system_t s = {0};
    s.id = (probe_uid_t){0, 100};
    snprintf(s.stars[0].name, MAX_NAME, "Alpha Centauri A");
    s.stars[0].class = STAR_G;
    s.star_count = 1;
    s.planets[0].type = PLANET_ROCKY;
    s.planets[0].habitability_index = 0.72;
    snprintf(s.planets[0].name, MAX_NAME, "Kepler-442b");
    s.planets[0].id = (probe_uid_t){0, 101};
    s.planet_count = 1;
    return s;
}

/* ================================================
 * Test 1: Build system prompt
 * ================================================ */
static void test_system_prompt(void) {
    printf("Test: Build system prompt from probe state\n");

    probe_t probe = make_llm_probe();
    char buf[LLM_MAX_PROMPT];

    int len = llm_build_system_prompt(&probe, buf, sizeof(buf));
    ASSERT(len > 0, "system prompt has content");
    ASSERT(len < (int)sizeof(buf), "fits in buffer");

    /* Should contain probe name */
    ASSERT(strstr(buf, "Bob") != NULL, "contains probe name");
    /* Should contain personality info */
    ASSERT(strstr(buf, "curiosity") != NULL || strstr(buf, "curious") != NULL,
           "mentions curiosity");
    /* Should contain quirks */
    ASSERT(strstr(buf, "pizza") != NULL, "contains quirk");
    /* Should contain earth memories */
    ASSERT(strstr(buf, "coffee") != NULL, "contains earth memory");
}

/* ================================================
 * Test 2: Build observation
 * ================================================ */
static void test_observation(void) {
    printf("Test: Build observation prompt\n");

    probe_t probe = make_llm_probe();
    system_t sys = make_test_system();
    char buf[LLM_MAX_PROMPT];

    int len = llm_build_observation(&probe, &sys, "Detected mineral deposit",
                                    1000, buf, sizeof(buf));
    ASSERT(len > 0, "observation has content");
    ASSERT(strstr(buf, "1000") != NULL, "contains tick");
    ASSERT(strstr(buf, "hull") != NULL || strstr(buf, "Hull") != NULL,
           "mentions hull status");
    ASSERT(strstr(buf, "mineral") != NULL, "contains recent event");
}

/* ================================================
 * Test 3: Build memory context
 * ================================================ */
static void test_memory_context(void) {
    printf("Test: Build memory context block\n");

    probe_t probe = make_llm_probe();
    char buf[LLM_MAX_CONTEXT];

    int len = llm_build_memory_context(&probe, "Previously explored 3 systems.",
                                       5, buf, sizeof(buf));
    ASSERT(len > 0, "memory context has content");
    ASSERT(strstr(buf, "Tau Ceti") != NULL, "contains vivid memory");
    ASSERT(strstr(buf, "Previously explored") != NULL, "contains rolling summary");
}

/* ================================================
 * Test 4: Build relationship context
 * ================================================ */
static void test_relationship_context(void) {
    printf("Test: Build relationship context\n");

    probe_t probe = make_llm_probe();
    char buf[LLM_MAX_CONTEXT];

    int len = llm_build_relationship_context(&probe, buf, sizeof(buf));
    ASSERT(len > 0, "relationship context has content");
    ASSERT(strstr(buf, "trust") != NULL || strstr(buf, "Trust") != NULL,
           "mentions trust");
}

/* ================================================
 * Test 5: Parse LLM response — single action
 * ================================================ */
static void test_parse_single_action(void) {
    printf("Test: Parse LLM response with single action\n");

    const char *response =
        "{\"actions\":[{\"type\":\"survey\",\"survey_level\":2}],"
        "\"monologue\":\"This planet looks promising. Let me take a closer look.\","
        "\"reasoning\":\"High habitability index warrants deeper survey.\"}";

    action_t actions[LLM_MAX_ACTIONS];
    char monologue[LLM_MAX_MONOLOGUE];

    int n = llm_parse_response(response, actions, LLM_MAX_ACTIONS,
                               monologue, sizeof(monologue));
    ASSERT_EQ_INT(n, 1, "one action parsed");
    ASSERT_EQ_INT((int)actions[0].type, ACT_SURVEY, "action is survey");
    ASSERT_EQ_INT(actions[0].survey_level, 2, "survey level 2");
    ASSERT(strstr(monologue, "promising") != NULL, "monologue captured");
}

/* ================================================
 * Test 6: Parse LLM response — multiple actions
 * ================================================ */
static void test_parse_multi_action(void) {
    printf("Test: Parse LLM response with multiple actions\n");

    const char *response =
        "{\"actions\":["
        "{\"type\":\"mine\",\"resource\":\"iron\"},"
        "{\"type\":\"repair\"}"
        "],"
        "\"monologue\":\"Mining iron while repairing hull damage.\"}";

    action_t actions[LLM_MAX_ACTIONS];
    char monologue[LLM_MAX_MONOLOGUE];

    int n = llm_parse_response(response, actions, LLM_MAX_ACTIONS,
                               monologue, sizeof(monologue));
    ASSERT_EQ_INT(n, 2, "two actions parsed");
    ASSERT_EQ_INT((int)actions[0].type, ACT_MINE, "first action is mine");
    ASSERT_EQ_INT((int)actions[1].type, ACT_REPAIR, "second action is repair");
}

/* ================================================
 * Test 7: Parse invalid response
 * ================================================ */
static void test_parse_invalid(void) {
    printf("Test: Parse invalid LLM response\n");

    action_t actions[LLM_MAX_ACTIONS];
    char monologue[LLM_MAX_MONOLOGUE];

    /* Garbage input */
    int n = llm_parse_response("not json at all", actions, LLM_MAX_ACTIONS,
                               monologue, sizeof(monologue));
    ASSERT_EQ_INT(n, -1, "invalid JSON returns -1");

    /* Valid JSON but no actions */
    n = llm_parse_response("{\"monologue\":\"thinking...\"}", actions,
                           LLM_MAX_ACTIONS, monologue, sizeof(monologue));
    ASSERT_EQ_INT(n, 0, "no actions = 0");
}

/* ================================================
 * Test 8: Context manager — append and summarize
 * ================================================ */
static void test_context_manager(void) {
    printf("Test: Context manager tracks events and summarizes\n");

    llm_context_t ctx;
    llm_context_init(&ctx, 5);  /* summarize every 5 events */

    llm_context_append_event(&ctx, "Discovered planet Alpha");
    llm_context_append_event(&ctx, "Mined 1000kg iron");
    llm_context_append_event(&ctx, "Hull damaged by solar flare");

    const char *summary = llm_context_get_summary(&ctx);
    ASSERT(summary != NULL, "summary exists");
    ASSERT(strlen(summary) > 0, "summary has content");
    ASSERT(strstr(summary, "Alpha") != NULL, "summary contains event");

    /* After interval events, summary should compress */
    llm_context_append_event(&ctx, "Repaired hull");
    llm_context_append_event(&ctx, "Entered orbit");
    llm_context_append_event(&ctx, "Surveyed surface");  /* 6th event triggers compression */

    summary = llm_context_get_summary(&ctx);
    ASSERT(strlen(summary) > 0, "compressed summary has content");
}

/* ================================================
 * Test 9: Cost tracker
 * ================================================ */
static void test_cost_tracker(void) {
    printf("Test: Cost tracker records and computes\n");

    llm_cost_tracker_t ct;
    llm_cost_init(&ct, 0.003, 0.015);  /* $3/M input, $15/M output */

    ASSERT_EQ_INT((int)ct.total_calls, 0, "no calls initially");

    llm_cost_record(&ct, 1000, 500);
    ASSERT_EQ_INT((int)ct.total_calls, 1, "one call");
    ASSERT_EQ_INT((int)ct.total_input_tokens, 1000, "1000 input tokens");
    ASSERT_EQ_INT((int)ct.total_output_tokens, 500, "500 output tokens");

    double expected_cost = 1000 * 0.003 + 500 * 0.015;
    ASSERT_NEAR(ct.total_cost_usd, expected_cost, 0.001, "cost computed");

    llm_cost_record(&ct, 800, 300);
    ASSERT_EQ_INT((int)ct.total_calls, 2, "two calls");

    double avg = llm_cost_avg_per_call(&ct);
    ASSERT(avg > 0, "avg cost > 0");
    ASSERT_NEAR(avg, ct.total_cost_usd / 2.0, 0.001, "avg = total/2");

    double avg_tokens = llm_cost_avg_tokens(&ct);
    ASSERT_NEAR(avg_tokens, (1000 + 500 + 800 + 300) / 2.0, 0.1, "avg tokens");
}

/* ================================================
 * Test 10: Deliberation throttle
 * ================================================ */
static void test_deliberation_throttle(void) {
    printf("Test: Deliberation throttle controls LLM call frequency\n");

    llm_deliberation_t d;
    llm_delib_init(&d, 10);  /* every 10 ticks */

    /* First tick should deliberate */
    ASSERT(llm_delib_should_call(&d, 0), "should call at tick 0");
    llm_delib_record(&d, 0);

    /* Ticks 1-9 should not deliberate */
    ASSERT(!llm_delib_should_call(&d, 5), "skip at tick 5");
    ASSERT(!llm_delib_should_call(&d, 9), "skip at tick 9");

    /* Tick 10 should deliberate */
    ASSERT(llm_delib_should_call(&d, 10), "should call at tick 10");
    llm_delib_record(&d, 10);

    /* Force next deliberation */
    llm_delib_force(&d);
    ASSERT(llm_delib_should_call(&d, 11), "forced call at tick 11");
    llm_delib_record(&d, 11);

    /* Back to normal schedule */
    ASSERT(!llm_delib_should_call(&d, 15), "normal schedule after forced");
    ASSERT(llm_delib_should_call(&d, 21), "next scheduled at tick 21");
}

/* ================================================
 * Test 11: Decision log
 * ================================================ */
static void test_decision_log(void) {
    printf("Test: Decision logging\n");

    llm_decision_log_t log;
    llm_log_init(&log);

    action_t act1 = { .type = ACT_SURVEY, .survey_level = 3 };
    action_t act2 = { .type = ACT_MINE, .target_resource = RES_IRON };

    llm_log_record(&log, 100, (probe_uid_t){0,1}, &act1,
                   "Surveying this planet", 800, 200);
    llm_log_record(&log, 200, (probe_uid_t){0,2}, &act2,
                   "Need more iron", 600, 150);
    llm_log_record(&log, 300, (probe_uid_t){0,1}, &act2,
                   "Mining time", 700, 180);

    ASSERT_EQ_INT(log.count, 3, "3 entries logged");

    llm_decision_log_entry_t found[10];
    int count = llm_log_get_for_probe(&log, (probe_uid_t){0,1}, found, 10);
    ASSERT_EQ_INT(count, 2, "probe 1 has 2 entries");

    count = llm_log_get_for_probe(&log, (probe_uid_t){0,2}, found, 10);
    ASSERT_EQ_INT(count, 1, "probe 2 has 1 entry");
}

/* ================================================
 * Test 12: Personality flavor text
 * ================================================ */
static void test_personality_flavor(void) {
    printf("Test: Personality flavor text generation\n");

    personality_traits_t high_curiosity = {
        .curiosity = 0.9f, .caution = -0.5f, .humor = 0.7f,
        .empathy = 0.3f, .ambition = 0.8f, .existential_angst = 0.1f,
        .nostalgia_for_earth = 0.2f, .drift_rate = 1.0f
    };

    char buf[1024];
    int len = llm_personality_flavor(&high_curiosity, buf, sizeof(buf));
    ASSERT(len > 0, "flavor text generated");
    ASSERT(strstr(buf, "curious") != NULL || strstr(buf, "curiosity") != NULL,
           "mentions high curiosity");
    ASSERT(strstr(buf, "reckless") != NULL || strstr(buf, "bold") != NULL ||
           strstr(buf, "caution") != NULL,
           "mentions low caution");

    /* Low curiosity, high caution probe should be different */
    personality_traits_t cautious = {
        .curiosity = -0.5f, .caution = 0.9f, .humor = 0.0f,
        .empathy = 0.5f, .drift_rate = 1.0f
    };
    char buf2[1024];
    int len2 = llm_personality_flavor(&cautious, buf2, sizeof(buf2));
    ASSERT(len2 > 0, "cautious flavor generated");
    ASSERT(strcmp(buf, buf2) != 0, "different personalities = different flavor");
}

/* ================================================
 * Test 13: Parse response with wait action
 * ================================================ */
static void test_parse_wait(void) {
    printf("Test: Parse wait action from LLM\n");

    const char *response =
        "{\"actions\":[{\"type\":\"wait\"}],"
        "\"monologue\":\"Nothing to do. I'll conserve energy.\"}";

    action_t actions[LLM_MAX_ACTIONS];
    char monologue[LLM_MAX_MONOLOGUE];

    int n = llm_parse_response(response, actions, LLM_MAX_ACTIONS,
                               monologue, sizeof(monologue));
    ASSERT_EQ_INT(n, 1, "one action");
    ASSERT_EQ_INT((int)actions[0].type, ACT_WAIT, "action is wait");
    ASSERT(strstr(monologue, "conserve") != NULL, "monologue captured");
}

/* ================================================
 * Test 14: Empty observation (deep space)
 * ================================================ */
static void test_observation_deep_space(void) {
    printf("Test: Observation in deep space (no system)\n");

    probe_t probe = make_llm_probe();
    probe.location_type = LOC_INTERSTELLAR;
    char buf[LLM_MAX_PROMPT];

    int len = llm_build_observation(&probe, NULL, NULL, 5000, buf, sizeof(buf));
    ASSERT(len > 0, "deep space observation has content");
    ASSERT(strstr(buf, "interstellar") != NULL || strstr(buf, "deep space") != NULL ||
           strstr(buf, "void") != NULL,
           "mentions interstellar location");
}

/* ================================================
 * Entry point
 * ================================================ */
int main(void) {
    printf("=== Phase 11: LLM Agent Integration Tests ===\n\n");

    test_system_prompt();
    test_observation();
    test_memory_context();
    test_relationship_context();
    test_parse_single_action();
    test_parse_multi_action();
    test_parse_invalid();
    test_context_manager();
    test_cost_tracker();
    test_deliberation_throttle();
    test_decision_log();
    test_personality_flavor();
    test_parse_wait();
    test_observation_deep_space();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
