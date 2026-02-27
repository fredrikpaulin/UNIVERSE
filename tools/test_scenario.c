/*
 * test_scenario.c — Phase 12: Polish & Scenario Framework tests
 *
 * Tests: event injection, metrics, snapshots, config, replay, forking.
 *
 * NOTE: universe_t is ~90MB, snapshot_t is ~90MB — all must be static/heap.
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <math.h>
#include "../src/scenario.h"
#include "../src/generate.h"
#include "../src/personality.h"

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

/* Shared static universe (reused across tests, re-inited each time) */
static universe_t g_uni;
static universe_t g_forked;
static snapshot_t g_snap1;
static snapshot_t g_snap2;

static void init_universe(universe_t *uni) {
    memset(uni, 0, sizeof(*uni));
    uni->seed = 42;
    uni->tick = 1000;
    uni->running = true;
    uni->probe_count = 2;

    uni->probes[0].id = (probe_uid_t){0, 1};
    snprintf(uni->probes[0].name, MAX_NAME, "Bob");
    uni->probes[0].status = STATUS_ACTIVE;
    uni->probes[0].hull_integrity = 1.0f;
    uni->probes[0].energy_joules = 500000.0;
    uni->probes[0].personality.drift_rate = 1.0f;
    for (int t = 0; t < TECH_COUNT; t++) uni->probes[0].tech_levels[t] = 3;

    uni->probes[1].id = (probe_uid_t){0, 2};
    snprintf(uni->probes[1].name, MAX_NAME, "Alice");
    uni->probes[1].status = STATUS_ACTIVE;
    uni->probes[1].hull_integrity = 0.8f;
    uni->probes[1].energy_joules = 300000.0;
    uni->probes[1].personality.drift_rate = 1.0f;
    for (int t = 0; t < TECH_COUNT; t++) uni->probes[1].tech_levels[t] = 5;
}

static system_t make_system(void) {
    system_t s = {0};
    s.id = (probe_uid_t){0, 100};
    s.star_count = 1;
    s.planet_count = 1;
    s.planets[0].type = PLANET_ROCKY;
    s.planets[0].habitability_index = 0.5;
    return s;
}

/* ================================================
 * Test 1: Injection queue init
 * ================================================ */
static void test_inject_init(void) {
    printf("Test: Injection queue initializes clean\n");

    injection_queue_t q;
    inject_init(&q);
    ASSERT_EQ_INT(q.count, 0, "no queued events");
}

/* ================================================
 * Test 2: Inject event and flush
 * ================================================ */
static void test_inject_and_flush(void) {
    printf("Test: Inject event and flush into event system\n");

    injection_queue_t q;
    inject_init(&q);

    int ret = inject_event(&q, EVT_CRISIS, CRISIS_SYSTEM_FAILURE,
                           "alien fleet detected", 0.9f, uid_null());
    ASSERT_EQ_INT(ret, 0, "inject succeeds");
    ASSERT_EQ_INT(q.count, 1, "one pending event");

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 42);

    init_universe(&g_uni);
    system_t sys = make_system();

    int flushed = inject_flush(&q, &es, g_uni.probes, g_uni.probe_count,
                               &sys, 1000, &rng);
    ASSERT(flushed > 0, "events flushed");
    ASSERT(es.count > 0, "event system has events");

    /* Queue should be empty after flush */
    ASSERT_EQ_INT(q.count, 0, "queue empty after flush");
}

/* ================================================
 * Test 3: Inject via JSON parsing
 * ================================================ */
static void test_inject_json(void) {
    printf("Test: Inject event via JSON string\n");

    injection_queue_t q;
    inject_init(&q);

    const char *json = "{\"type\":\"hazard\",\"subtype\":0,"
                       "\"description\":\"Massive solar storm incoming\","
                       "\"severity\":0.85}";

    int ret = inject_parse_json(&q, json);
    ASSERT_EQ_INT(ret, 0, "JSON parse succeeds");
    ASSERT_EQ_INT(q.count, 1, "one event queued");
    ASSERT_EQ_INT((int)q.events[0].type, EVT_HAZARD, "type is hazard");
    ASSERT(q.events[0].severity > 0.8f, "severity parsed");
    ASSERT(strstr(q.events[0].description, "solar storm") != NULL, "description parsed");
}

/* ================================================
 * Test 4: Metrics init and record
 * ================================================ */
static void test_metrics_record(void) {
    printf("Test: Metrics recording\n");

    metrics_system_t ms;
    metrics_init(&ms, 10);  /* record every 10 ticks */

    init_universe(&g_uni);
    event_system_t es;
    events_init(&es);

    /* Should record at tick 0 (aligned with interval) */
    metrics_record(&ms, &g_uni, &es, 0);
    ASSERT_EQ_INT(ms.count, 1, "one metrics snapshot");

    /* Should NOT record at tick 5 */
    metrics_record(&ms, &g_uni, &es, 5);
    ASSERT_EQ_INT(ms.count, 1, "still one (not aligned)");

    /* Should record at tick 10 */
    metrics_record(&ms, &g_uni, &es, 10);
    ASSERT_EQ_INT(ms.count, 2, "two snapshots at tick 10");
}

/* ================================================
 * Test 5: Metrics compute avg tech level
 * ================================================ */
static void test_metrics_avg_tech(void) {
    printf("Test: Average tech level computation\n");

    init_universe(&g_uni);
    /* Probe 0: all tech 3, Probe 1: all tech 5
     * Average per probe: (3 + 5) / 2 = 4.0 */
    double avg = metrics_avg_tech(&g_uni);
    ASSERT_NEAR(avg, 4.0, 0.01, "avg tech level = 4.0");
}

/* ================================================
 * Test 6: Metrics latest
 * ================================================ */
static void test_metrics_latest(void) {
    printf("Test: Get latest metrics\n");

    metrics_system_t ms;
    metrics_init(&ms, 1);

    init_universe(&g_uni);
    event_system_t es;
    events_init(&es);

    metrics_record(&ms, &g_uni, &es, 100);
    g_uni.tick = 200;
    metrics_record(&ms, &g_uni, &es, 200);

    const metrics_snapshot_t *latest = metrics_latest(&ms);
    ASSERT(latest != NULL, "latest exists");
    if (latest) {
        ASSERT_EQ_INT((int)latest->tick, 200, "latest is tick 200");
        ASSERT_EQ_INT((int)latest->probes_spawned, 2, "2 probes");
    }
}

/* ================================================
 * Test 7: Snapshot take and restore
 * ================================================ */
static void test_snapshot(void) {
    printf("Test: Snapshot and restore\n");

    init_universe(&g_uni);

    snapshot_take(&g_snap1, &g_uni, "test_snap_1000");
    ASSERT(g_snap1.valid, "snapshot valid");
    ASSERT_EQ_INT((int)g_snap1.tick, 1000, "snapshot tick");
    ASSERT(strcmp(g_snap1.tag, "test_snap_1000") == 0, "snapshot tag");
    ASSERT_EQ_INT((int)g_snap1.probe_count, 2, "snapshot has 2 probes");

    /* Modify universe */
    g_uni.tick = 2000;
    g_uni.probe_count = 1;
    g_uni.probes[0].hull_integrity = 0.1f;

    /* Restore */
    int ret = snapshot_restore(&g_snap1, &g_uni);
    ASSERT_EQ_INT(ret, 0, "restore succeeds");
    ASSERT_EQ_INT((int)g_uni.tick, 1000, "tick restored");
    ASSERT_EQ_INT((int)g_uni.probe_count, 2, "probe count restored");
    ASSERT_NEAR(g_uni.probes[0].hull_integrity, 1.0, 0.01, "hull restored");
}

/* ================================================
 * Test 8: Snapshot matches verification
 * ================================================ */
static void test_snapshot_matches(void) {
    printf("Test: Snapshot comparison\n");

    init_universe(&g_uni);

    snapshot_take(&g_snap1, &g_uni, "snap_a");
    snapshot_take(&g_snap2, &g_uni, "snap_b");

    ASSERT(snapshot_matches(&g_snap1, &g_snap2), "same state = matches");

    /* Modify and retake */
    g_uni.probes[0].hull_integrity = 0.5f;
    snapshot_take(&g_snap2, &g_uni, "snap_c");
    ASSERT(!snapshot_matches(&g_snap1, &g_snap2), "different state = no match");
}

/* ================================================
 * Test 9: Snapshot → run → rollback → verify
 * ================================================ */
static void test_snapshot_rollback_cycle(void) {
    printf("Test: Snapshot -> modify -> rollback -> state matches\n");

    init_universe(&g_uni);
    snapshot_take(&g_snap1, &g_uni, "before");

    /* Simulate 1000 ticks of changes */
    g_uni.tick += 1000;
    g_uni.probes[0].hull_integrity = 0.3f;
    g_uni.probes[0].energy_joules = 100.0;
    g_uni.probes[1].status = STATUS_DESTROYED;

    /* Rollback */
    snapshot_restore(&g_snap1, &g_uni);

    /* Verify */
    snapshot_take(&g_snap2, &g_uni, "after");

    ASSERT(snapshot_matches(&g_snap1, &g_snap2),
           "state matches after rollback");
}

/* ================================================
 * Test 10: Universe fork
 * ================================================ */
static void test_universe_fork(void) {
    printf("Test: Universe fork with new seed\n");

    init_universe(&g_uni);
    snapshot_take(&g_snap1, &g_uni, "fork_point");

    memset(&g_forked, 0, sizeof(g_forked));
    int ret = universe_fork(&g_snap1, &g_forked, 99);
    ASSERT_EQ_INT(ret, 0, "fork succeeds");
    ASSERT_EQ_INT((int)g_forked.seed, 99, "new seed applied");
    ASSERT_EQ_INT((int)g_forked.tick, (int)g_snap1.tick, "same tick");
    ASSERT_EQ_INT((int)g_forked.probe_count, (int)g_snap1.probe_count, "same probes");

    /* Forked universe has different seed but same probes */
    ASSERT(g_forked.seed != g_uni.seed, "seeds differ");
    ASSERT(uid_eq(g_forked.probes[0].id, g_uni.probes[0].id), "probe IDs match");
}

/* ================================================
 * Test 11: Config init and get
 * ================================================ */
static void test_config(void) {
    printf("Test: Configuration system\n");

    config_t cfg;
    config_init(&cfg);

    /* Set and get */
    config_set(&cfg, "event_freq_discovery", "0.01");
    config_set(&cfg, "mutation_rate", "0.15");

    const char *val = config_get(&cfg, "event_freq_discovery");
    ASSERT(val != NULL, "key found");
    if (val) ASSERT(strcmp(val, "0.01") == 0, "value correct");

    double d = config_get_double(&cfg, "mutation_rate", 0.1);
    ASSERT_NEAR(d, 0.15, 0.001, "double value correct");

    /* Default for missing key */
    double def = config_get_double(&cfg, "nonexistent", 42.0);
    ASSERT_NEAR(def, 42.0, 0.001, "default for missing key");
}

/* ================================================
 * Test 12: Config parse JSON
 * ================================================ */
static void test_config_json(void) {
    printf("Test: Config parse from JSON\n");

    config_t cfg;
    config_init(&cfg);

    const char *json = "{\"tick_rate\":60,\"event_freq_discovery\":0.008,"
                       "\"repl_base_ticks\":250}";

    int count = config_parse_json(&cfg, json);
    ASSERT(count >= 3, "parsed at least 3 entries");

    double tick_rate = config_get_double(&cfg, "tick_rate", 0);
    ASSERT_NEAR(tick_rate, 60.0, 0.1, "tick_rate parsed");

    double freq = config_get_double(&cfg, "event_freq_discovery", 0);
    ASSERT_NEAR(freq, 0.008, 0.001, "event freq parsed");
}

/* ================================================
 * Test 13: Config changes affect simulation parameter
 * ================================================ */
static void test_config_affects_sim(void) {
    printf("Test: Config value used as simulation parameter\n");

    config_t cfg;
    config_init(&cfg);

    /* Default discovery frequency */
    double default_freq = config_get_double(&cfg, "event_freq_discovery", 0.005);
    ASSERT_NEAR(default_freq, 0.005, 0.001, "default freq");

    /* Override */
    config_set(&cfg, "event_freq_discovery", "0.05");
    double new_freq = config_get_double(&cfg, "event_freq_discovery", 0.005);
    ASSERT_NEAR(new_freq, 0.05, 0.001, "overridden freq");
    ASSERT(new_freq > default_freq, "override is higher");
}

/* ================================================
 * Test 14: Replay init and step
 * ================================================ */
static void test_replay(void) {
    printf("Test: Replay event playback\n");

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 42);

    /* Add some events at different ticks */
    init_universe(&g_uni);
    system_t sys = make_system();

    events_generate(&es, &g_uni.probes[0], EVT_DISCOVERY, 0, &sys, 100, &rng);
    events_generate(&es, &g_uni.probes[0], EVT_HAZARD, 0, &sys, 200, &rng);
    events_generate(&es, &g_uni.probes[0], EVT_WONDER, 0, &sys, 300, &rng);

    /* Replay ticks 50-250 */
    replay_t rep;
    replay_init(&rep, &es, 50, 250);
    ASSERT(rep.active, "replay is active");
    ASSERT_EQ_INT(rep.event_count, 2, "2 events in range (100, 200)");

    /* Step through */
    sim_event_t out[10];
    int total_events = 0;
    while (!replay_done(&rep)) {
        int n = replay_step(&rep, out, 10);
        total_events += n;
    }
    ASSERT_EQ_INT(total_events, 2, "replayed 2 events total");
    ASSERT(replay_done(&rep), "replay is done");
}

/* ================================================
 * Test 15: Replay empty range
 * ================================================ */
static void test_replay_empty(void) {
    printf("Test: Replay with no events in range\n");

    event_system_t es;
    events_init(&es);

    replay_t rep;
    replay_init(&rep, &es, 0, 100);
    ASSERT_EQ_INT(rep.event_count, 0, "no events to replay");
    ASSERT(replay_done(&rep), "immediately done");
}

/* ================================================
 * Test 16: Inject targeted to specific probe
 * ================================================ */
static void test_inject_targeted(void) {
    printf("Test: Inject event targeted at specific probe\n");

    injection_queue_t q;
    inject_init(&q);

    probe_uid_t target = {0, 1};
    inject_event(&q, EVT_HAZARD, HAZ_SOLAR_FLARE,
                 "targeted flare", 0.7f, target);

    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, 42);

    init_universe(&g_uni);
    g_uni.probes[0].hull_integrity = 1.0f;
    g_uni.probes[1].hull_integrity = 1.0f;
    system_t sys = make_system();

    inject_flush(&q, &es, g_uni.probes, g_uni.probe_count, &sys, 1000, &rng);

    /* Only probe 0 (id=1) should be affected */
    ASSERT(g_uni.probes[0].hull_integrity < 1.0f, "targeted probe damaged");
    /* Probe 1 should be unaffected */
    ASSERT_NEAR(g_uni.probes[1].hull_integrity, 1.0, 0.01, "other probe undamaged");
}

/* ================================================
 * Test 17: Metrics avg trust
 * ================================================ */
static void test_metrics_avg_trust(void) {
    printf("Test: Average trust computation\n");

    init_universe(&g_uni);
    /* Add relationships */
    g_uni.probes[0].relationships[0].other_id = g_uni.probes[1].id;
    g_uni.probes[0].relationships[0].trust = 0.6f;
    g_uni.probes[0].relationship_count = 1;

    g_uni.probes[1].relationships[0].other_id = g_uni.probes[0].id;
    g_uni.probes[1].relationships[0].trust = 0.4f;
    g_uni.probes[1].relationship_count = 1;

    float avg = metrics_avg_trust(&g_uni);
    ASSERT_NEAR(avg, 0.5, 0.01, "avg trust = 0.5");
}

/* ================================================
 * Test 18: Invalid snapshot restore
 * ================================================ */
static void test_invalid_snapshot(void) {
    printf("Test: Invalid snapshot restore rejected\n");

    memset(&g_snap1, 0, sizeof(g_snap1));
    g_snap1.valid = false;

    init_universe(&g_uni);
    int ret = snapshot_restore(&g_snap1, &g_uni);
    ASSERT_EQ_INT(ret, -1, "invalid snapshot rejected");
}

/* ================================================
 * Entry point
 * ================================================ */
int main(void) {
    printf("=== Phase 12: Scenario & Polish Tests ===\n\n");

    test_inject_init();
    test_inject_and_flush();
    test_inject_json();
    test_metrics_record();
    test_metrics_avg_tech();
    test_metrics_latest();
    test_snapshot();
    test_snapshot_matches();
    test_snapshot_rollback_cycle();
    test_universe_fork();
    test_config();
    test_config_json();
    test_config_affects_sim();
    test_replay();
    test_replay_empty();
    test_inject_targeted();
    test_metrics_avg_trust();
    test_invalid_snapshot();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
