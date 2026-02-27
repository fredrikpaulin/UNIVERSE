#define _POSIX_C_SOURCE 199309L
/*
 * test_agent.c — Phase 4 tests: agent IPC protocol, observation/action JSON,
 *                fallback agent, and socket integration.
 *
 * Written BEFORE implementation. Defines the agent_ipc.h API contract.
 *
 * Tests the spec requirements:
 *   - Observation JSON contains probe state, surroundings, nearby objects
 *   - Action JSON parsed into action_t
 *   - Invalid action → error response
 *   - Fallback agent: idle/survive mode (repair if damaged, wait otherwise)
 *   - Agent registration by probe ID
 *   - Timeout → probe idles
 *   - Socket round-trip (via socketpair)
 */

/* Include universe.h FIRST to define our probe_uid_t before sys/types.h */
#include "universe.h"

#include "rng.h"
#include "generate.h"
#include "persist.h"
#include "probe.h"
#include "travel.h"
#include "agent_ipc.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>
#include <unistd.h>
#include <sys/socket.h>

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

/* ---- Test: Observation serialization ---- */
static void test_obs_serialize(void) {
    printf("Test: Observation serialization\n");

    probe_t bob;
    probe_init_bob(&bob);
    bob.location_type = LOC_IN_SYSTEM;
    bob.fuel_kg = 40000.0;
    bob.energy_joules = 5.0e11;
    bob.hull_integrity = 0.95f;

    system_t systems[30];
    int count = generate_sector(systems, 30, 42, (sector_coord_t){0, 0, 0});
    ASSERT(count > 0, "Has systems");

    bob.system_id = systems[0].id;
    bob.sector = systems[0].sector;

    /* Serialize */
    char buf[8192];
    int len = obs_serialize(&bob, &systems[0], buf, sizeof(buf));
    ASSERT(len > 0, "Serialization produced output");
    ASSERT(len < (int)sizeof(buf), "Fits in buffer");

    /* Basic JSON structure checks */
    ASSERT(buf[0] == '{', "Starts with {");
    ASSERT(buf[len-1] == '}', "Ends with }");
    ASSERT(strstr(buf, "\"probe\"") != NULL, "Contains probe section");
    ASSERT(strstr(buf, "\"system\"") != NULL, "Contains system section");
    ASSERT(strstr(buf, "\"tick\"") != NULL, "Contains tick field");

    /* Probe fields present */
    ASSERT(strstr(buf, "\"name\"") != NULL, "Contains name");
    ASSERT(strstr(buf, "\"Bob\"") != NULL, "Name is Bob");
    ASSERT(strstr(buf, "\"fuel_kg\"") != NULL, "Contains fuel");
    ASSERT(strstr(buf, "\"hull_integrity\"") != NULL, "Contains hull");
    ASSERT(strstr(buf, "\"location_type\"") != NULL, "Contains location type");
    ASSERT(strstr(buf, "\"energy_joules\"") != NULL, "Contains energy");

    /* System fields present */
    ASSERT(strstr(buf, "\"star_count\"") != NULL, "Contains star_count");
    ASSERT(strstr(buf, "\"planet_count\"") != NULL, "Contains planet_count");

    printf("  Serialized %d bytes\n", len);
}

/* ---- Test: Action parsing from JSON ---- */
static void test_action_parse(void) {
    printf("Test: Action parsing from JSON\n");

    /* Parse a mine action */
    const char *mine_json = "{\"action\":\"mine\",\"resource\":\"iron\"}";
    action_t act;
    int ok = action_parse(mine_json, &act);
    ASSERT(ok == 0, "Mine action parsed");
    ASSERT(act.type == ACT_MINE, "Type is mine");
    ASSERT(act.target_resource == RES_IRON, "Resource is iron");

    /* Parse a survey action */
    const char *survey_json = "{\"action\":\"survey\",\"level\":2}";
    ok = action_parse(survey_json, &act);
    ASSERT(ok == 0, "Survey action parsed");
    ASSERT(act.type == ACT_SURVEY, "Type is survey");
    ASSERT(act.survey_level == 2, "Level is 2");

    /* Parse a wait action */
    const char *wait_json = "{\"action\":\"wait\"}";
    ok = action_parse(wait_json, &act);
    ASSERT(ok == 0, "Wait action parsed");
    ASSERT(act.type == ACT_WAIT, "Type is wait");

    /* Parse enter_orbit */
    const char *orbit_json = "{\"action\":\"enter_orbit\"}";
    ok = action_parse(orbit_json, &act);
    ASSERT(ok == 0, "Orbit action parsed");
    ASSERT(act.type == ACT_ENTER_ORBIT, "Type is enter_orbit");

    /* Parse land */
    const char *land_json = "{\"action\":\"land\"}";
    ok = action_parse(land_json, &act);
    ASSERT(ok == 0, "Land action parsed");
    ASSERT(act.type == ACT_LAND, "Type is land");

    /* Parse launch */
    const char *launch_json = "{\"action\":\"launch\"}";
    ok = action_parse(launch_json, &act);
    ASSERT(ok == 0, "Launch action parsed");
    ASSERT(act.type == ACT_LAUNCH, "Type is launch");

    /* Parse repair */
    const char *repair_json = "{\"action\":\"repair\"}";
    ok = action_parse(repair_json, &act);
    ASSERT(ok == 0, "Repair action parsed");
    ASSERT(act.type == ACT_REPAIR, "Type is repair");

    /* Parse unknown action → error */
    const char *bad_json = "{\"action\":\"explode\"}";
    ok = action_parse(bad_json, &act);
    ASSERT(ok != 0, "Unknown action rejected");

    /* Parse garbage → error */
    const char *garbage = "not json at all";
    ok = action_parse(garbage, &act);
    ASSERT(ok != 0, "Garbage rejected");

    /* Parse empty → error */
    ok = action_parse("", &act);
    ASSERT(ok != 0, "Empty string rejected");
}

/* ---- Test: Action result serialization ---- */
static void test_result_serialize(void) {
    printf("Test: Action result serialization\n");

    action_result_t res = { .success = true, .completed = true, .error = "" };
    char buf[1024];
    int len = result_serialize(&res, buf, sizeof(buf));
    ASSERT(len > 0, "Result serialized");
    ASSERT(strstr(buf, "\"success\"") != NULL, "Contains success");
    ASSERT(strstr(buf, "true") != NULL, "Success is true");

    /* Error result */
    action_result_t err = { .success = false, .completed = false, .error = "Can't mine in orbit" };
    len = result_serialize(&err, buf, sizeof(buf));
    ASSERT(len > 0, "Error result serialized");
    ASSERT(strstr(buf, "false") != NULL, "Success is false");
    ASSERT(strstr(buf, "Can't mine in orbit") != NULL, "Error message present");
}

/* ---- Test: Fallback agent behavior ---- */
static void test_fallback_agent(void) {
    printf("Test: Fallback agent (idle/survive mode)\n");

    /* Healthy probe → should wait */
    probe_t bob;
    probe_init_bob(&bob);
    bob.location_type = LOC_IN_SYSTEM;
    bob.hull_integrity = 1.0f;

    action_t act = fallback_agent_decide(&bob);
    ASSERT(act.type == ACT_WAIT, "Healthy probe waits");

    /* Damaged probe → should repair */
    bob.hull_integrity = 0.5f;
    act = fallback_agent_decide(&bob);
    ASSERT(act.type == ACT_REPAIR, "Damaged probe repairs");

    /* Very damaged probe → still repair */
    bob.hull_integrity = 0.1f;
    act = fallback_agent_decide(&bob);
    ASSERT(act.type == ACT_REPAIR, "Very damaged probe repairs");

    /* Traveling probe → should wait (can't repair while traveling) */
    bob.status = STATUS_TRAVELING;
    bob.location_type = LOC_INTERSTELLAR;
    bob.hull_integrity = 0.5f;
    act = fallback_agent_decide(&bob);
    ASSERT(act.type == ACT_WAIT, "Traveling probe waits even if damaged");
}

/* ---- Test: Resource name parsing ---- */
static void test_resource_names(void) {
    printf("Test: Resource name round-trip\n");

    /* Check all resource names can be looked up */
    const char *names[] = {
        "iron", "silicon", "rare_earth", "water",
        "hydrogen", "helium3", "carbon", "uranium", "exotic"
    };
    for (int i = 0; i < RES_COUNT; i++) {
        resource_t r = resource_from_name(names[i]);
        ASSERT((int)r == i, "Resource name maps correctly");

        const char *back = resource_to_name(r);
        ASSERT(strcmp(back, names[i]) == 0, "Resource name round-trips");
    }

    /* Unknown resource */
    resource_t bad = resource_from_name("unobtanium");
    ASSERT((int)bad == -1, "Unknown resource returns -1");
}

/* ---- Test: Action name parsing ---- */
static void test_action_names(void) {
    printf("Test: Action name round-trip\n");

    const char *names[] = {
        "navigate_to_body", "enter_orbit", "land", "launch",
        "survey", "mine", "wait", "repair"
    };
    for (int i = 0; i < ACT_COUNT; i++) {
        action_type_t a = action_type_from_name(names[i]);
        ASSERT((int)a == i, "Action name maps correctly");

        const char *back = action_type_to_name(a);
        ASSERT(strcmp(back, names[i]) == 0, "Action name round-trips");
    }

    action_type_t bad = action_type_from_name("self_destruct");
    ASSERT((int)bad == -1, "Unknown action returns -1");
}

/* ---- Test: Protocol framing (newline-delimited JSON) ---- */
static void test_protocol_framing(void) {
    printf("Test: Protocol framing\n");

    /* Frame a message */
    const char *msg = "{\"action\":\"wait\"}";
    char framed[256];
    int flen = protocol_frame(msg, framed, sizeof(framed));
    ASSERT(flen > 0, "Framing produced output");
    ASSERT(framed[flen-1] == '\n', "Frame ends with newline");
    /* Content before newline should be the original message */
    ASSERT(flen == (int)strlen(msg) + 1, "Frame length = msg + newline");
    ASSERT(strncmp(framed, msg, strlen(msg)) == 0, "Content preserved");

    /* Unframe: extract message from buffer with trailing newline */
    char unframed[256];
    int ulen = protocol_unframe(framed, flen, unframed, sizeof(unframed));
    ASSERT(ulen == (int)strlen(msg), "Unframe length matches");
    ASSERT(strcmp(unframed, msg) == 0, "Unframed content matches original");
}

/* ---- Test: Agent router — register and route by probe ID ---- */
static void test_agent_router(void) {
    printf("Test: Agent router (register/lookup by probe ID)\n");

    agent_router_t router;
    agent_router_init(&router);

    probe_uid_t probe1 = {1, 1};
    probe_uid_t probe2 = {2, 2};
    probe_uid_t probe3 = {3, 3}; /* not registered */

    /* Register agents with fake fd numbers */
    int ok = agent_router_register(&router, probe1, 10);
    ASSERT(ok == 0, "Register probe1 succeeds");
    ok = agent_router_register(&router, probe2, 20);
    ASSERT(ok == 0, "Register probe2 succeeds");

    /* Look up */
    int fd1 = agent_router_lookup(&router, probe1);
    ASSERT(fd1 == 10, "Probe1 routes to fd 10");
    int fd2 = agent_router_lookup(&router, probe2);
    ASSERT(fd2 == 20, "Probe2 routes to fd 20");

    /* Unregistered probe returns -1 */
    int fd3 = agent_router_lookup(&router, probe3);
    ASSERT(fd3 == -1, "Unregistered probe returns -1");

    /* Unregister */
    agent_router_unregister(&router, probe1);
    fd1 = agent_router_lookup(&router, probe1);
    ASSERT(fd1 == -1, "Unregistered probe1 returns -1");

    /* Probe2 still works */
    fd2 = agent_router_lookup(&router, probe2);
    ASSERT(fd2 == 20, "Probe2 still active");

    agent_router_destroy(&router);
}

/* ---- Test: Socket round-trip via socketpair ---- */
static void test_socket_roundtrip(void) {
    printf("Test: Socket round-trip (socketpair)\n");

    int sv[2];
    int rc = socketpair(AF_UNIX, SOCK_STREAM, 0, sv);
    ASSERT(rc == 0, "socketpair created");

    /* Simulate server sending observation */
    const char *obs = "{\"tick\":100,\"probe\":{\"name\":\"Bob\"}}\n";
    ssize_t sent = write(sv[0], obs, strlen(obs));
    ASSERT(sent == (ssize_t)strlen(obs), "Observation sent");

    /* Simulate agent reading */
    char buf[4096];
    ssize_t got = read(sv[1], buf, sizeof(buf) - 1);
    ASSERT(got > 0, "Agent received data");
    buf[got] = '\0';
    ASSERT(strstr(buf, "Bob") != NULL, "Received observation contains Bob");

    /* Agent sends action back */
    const char *response = "{\"action\":\"wait\"}\n";
    sent = write(sv[1], response, strlen(response));
    ASSERT(sent == (ssize_t)strlen(response), "Action sent");

    /* Server reads */
    got = read(sv[0], buf, sizeof(buf) - 1);
    ASSERT(got > 0, "Server received response");
    buf[got] = '\0';

    /* Parse the action */
    /* Strip newline for parsing */
    char *nl = strchr(buf, '\n');
    if (nl) *nl = '\0';
    action_t act;
    int ok = action_parse(buf, &act);
    ASSERT(ok == 0, "Action parsed from socket data");
    ASSERT(act.type == ACT_WAIT, "Parsed wait action");

    close(sv[0]);
    close(sv[1]);
}

/* ---- Test: Full protocol cycle (observation → action → result) ---- */
static void test_full_protocol_cycle(void) {
    printf("Test: Full protocol cycle\n");

    probe_t bob;
    probe_init_bob(&bob);
    bob.location_type = LOC_ORBITING;
    bob.hull_integrity = 0.9f;

    system_t systems[30];
    int count = generate_sector(systems, 30, 42, (sector_coord_t){0, 0, 0});
    ASSERT(count > 0, "Has systems");

    bob.system_id = systems[0].id;
    bob.body_id = systems[0].planets[0].id;

    /* Step 1: Build observation JSON */
    char obs_buf[8192];
    int obs_len = obs_serialize(&bob, &systems[0], obs_buf, sizeof(obs_buf));
    ASSERT(obs_len > 0, "Observation built");

    /* Step 2: Parse an action that an agent might send */
    const char *agent_response = "{\"action\":\"survey\",\"level\":0}";
    action_t act;
    int ok = action_parse(agent_response, &act);
    ASSERT(ok == 0, "Agent action parsed");
    ASSERT(act.type == ACT_SURVEY, "Action is survey");

    /* Step 3: Execute action */
    action_result_t res = probe_execute_action(&bob, &act, &systems[0]);
    /* Whether this succeeds depends on probe state, but it shouldn't crash */
    (void)res;
    tests_passed++; /* if we got here without crash, good */

    /* Step 4: Serialize result */
    char res_buf[1024];
    int res_len = result_serialize(&res, res_buf, sizeof(res_buf));
    ASSERT(res_len > 0, "Result serialized");
    ASSERT(strstr(res_buf, "\"success\"") != NULL, "Result has success field");
}

/* ---- Test: Navigate to body action parsing with body ID ---- */
static void test_parse_navigate_with_body(void) {
    printf("Test: Parse navigate_to_body with target\n");

    const char *json = "{\"action\":\"navigate_to_body\",\"target_body_hi\":123,\"target_body_lo\":456}";
    action_t act;
    int ok = action_parse(json, &act);
    ASSERT(ok == 0, "Navigate action parsed");
    ASSERT(act.type == ACT_NAVIGATE_TO_BODY, "Type is navigate");
    ASSERT(act.target_body.hi == 123, "Body hi matches");
    ASSERT(act.target_body.lo == 456, "Body lo matches");
}

/* ---- Main ---- */
int main(void) {
    printf("=== Phase 4: Agent IPC Tests ===\n\n");

    test_obs_serialize();
    printf("\n");
    test_action_parse();
    printf("\n");
    test_result_serialize();
    printf("\n");
    test_fallback_agent();
    printf("\n");
    test_resource_names();
    printf("\n");
    test_action_names();
    printf("\n");
    test_protocol_framing();
    printf("\n");
    test_agent_router();
    printf("\n");
    test_socket_roundtrip();
    printf("\n");
    test_full_protocol_cycle();
    printf("\n");
    test_parse_navigate_with_body();

    printf("\n=== Results: %d passed, %d failed ===\n",
        tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
