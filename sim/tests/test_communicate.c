/*
 * test_communicate.c — Phase 8: Communication tests
 *
 * Tests: light-speed messaging, beacons, relay satellites, broadcast
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/communicate.h"
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

/* Helper: make a probe at a position with given comm tech level */
static probe_t make_probe(uint64_t id_lo, double x, double y, double z, uint8_t comm_level) {
    probe_t p = {0};
    p.id = (probe_uid_t){0, id_lo};
    p.location_type = LOC_IN_SYSTEM;
    p.status = STATUS_ACTIVE;
    p.tech_levels[TECH_COMMUNICATION] = comm_level;
    p.energy_joules = 1000000.0;  /* plenty of energy */
    p.sector = (sector_coord_t){0, 0, 0};
    /* Use heading as position proxy — in the real sim, position comes from sector+system */
    p.destination = (vec3_t){x, y, z};
    /* Also set heading to same for tests that use it */
    p.heading = (vec3_t){x, y, z};
    return p;
}

static vec3_t pos(double x, double y, double z) {
    return (vec3_t){x, y, z};
}

/* ================================================
 * Test 1: Communication range calculation
 * ================================================ */
static void test_comm_range(void) {
    printf("Test: Communication range scales with tech level\n");

    probe_t p = make_probe(1, 0, 0, 0, 1);
    double r1 = comm_range(&p);
    ASSERT_NEAR(r1, COMM_BASE_RANGE_LY + COMM_RANGE_PER_LEVEL * 1, 0.01,
                "range at tech level 1");

    p.tech_levels[TECH_COMMUNICATION] = 5;
    double r5 = comm_range(&p);
    ASSERT_NEAR(r5, COMM_BASE_RANGE_LY + COMM_RANGE_PER_LEVEL * 5, 0.01,
                "range at tech level 5");

    p.tech_levels[TECH_COMMUNICATION] = 10;
    double r10 = comm_range(&p);
    ASSERT_NEAR(r10, COMM_BASE_RANGE_LY + COMM_RANGE_PER_LEVEL * 10, 0.01,
                "range at tech level 10");

    ASSERT(r10 > r5, "higher tech = longer range");
    ASSERT(r5 > r1, "higher tech = longer range (mid)");
}

/* ================================================
 * Test 2: Light delay calculation
 * ================================================ */
static void test_light_delay(void) {
    printf("Test: Light delay matches distance / c\n");

    /* 10 ly apart → 10 years → 3650 ticks */
    uint64_t delay = comm_light_delay(pos(0, 0, 0), pos(10, 0, 0));
    ASSERT_EQ_INT((int)delay, 3650, "10 ly = 3650 ticks");

    /* 1 ly apart → 365 ticks */
    delay = comm_light_delay(pos(0, 0, 0), pos(1, 0, 0));
    ASSERT_EQ_INT((int)delay, 365, "1 ly = 365 ticks");

    /* Same position → 0 ticks */
    delay = comm_light_delay(pos(5, 5, 5), pos(5, 5, 5));
    ASSERT_EQ_INT((int)delay, 0, "same position = 0 delay");

    /* 3D distance: sqrt(3^2 + 4^2) = 5 ly → 1825 ticks */
    delay = comm_light_delay(pos(0, 0, 0), pos(3, 4, 0));
    ASSERT_EQ_INT((int)delay, 1825, "5 ly diagonal = 1825 ticks");
}

/* ================================================
 * Test 3: Send targeted message, arrives after light delay
 * ================================================ */
static void test_targeted_message(void) {
    printf("Test: Targeted message with light-speed delay\n");

    comm_system_t cs;
    comm_init(&cs);

    /* Bob at origin, child 10 ly away */
    probe_t bob = make_probe(1, 0, 0, 0, 5);
    snprintf(bob.name, MAX_NAME, "Bob");
    vec3_t child_pos = pos(10, 0, 0);
    probe_uid_t child_id = {0, 2};

    int ret = comm_send_targeted(&cs, &bob, child_id, child_pos,
                                 "Hello from Bob!", 1000);
    ASSERT_EQ_INT(ret, 0, "send targeted succeeds");
    ASSERT_EQ_INT(cs.count, 1, "one message in queue");
    ASSERT_EQ_INT((int)cs.messages[0].arrival_tick, 1000 + 3650,
                  "arrival = sent + 3650 ticks");
    ASSERT_EQ_INT((int)cs.messages[0].status, MSG_IN_TRANSIT, "status is in_transit");
    ASSERT(strcmp(cs.messages[0].content, "Hello from Bob!") == 0, "content preserved");

    /* Energy should be deducted */
    ASSERT(bob.energy_joules < 1000000.0, "energy deducted");
}

/* ================================================
 * Test 4: Message delivery on tick
 * ================================================ */
static void test_message_delivery(void) {
    printf("Test: Message delivered when arrival_tick reached\n");

    comm_system_t cs;
    comm_init(&cs);

    probe_t bob = make_probe(1, 0, 0, 0, 5);
    vec3_t child_pos = pos(1, 0, 0);  /* 1 ly away = 365 ticks */
    probe_uid_t child_id = {0, 2};

    comm_send_targeted(&cs, &bob, child_id, child_pos, "test msg", 100);

    /* Before arrival: nothing delivered */
    int delivered = comm_tick_deliver(&cs, 200);
    ASSERT_EQ_INT(delivered, 0, "not delivered before arrival_tick");
    ASSERT_EQ_INT((int)cs.messages[0].status, MSG_IN_TRANSIT, "still in transit");

    /* At arrival tick (100 + 365 = 465): delivered */
    delivered = comm_tick_deliver(&cs, 465);
    ASSERT_EQ_INT(delivered, 1, "delivered at arrival_tick");
    ASSERT_EQ_INT((int)cs.messages[0].status, MSG_DELIVERED, "status is delivered");

    /* Already delivered: not delivered again */
    delivered = comm_tick_deliver(&cs, 466);
    ASSERT_EQ_INT(delivered, 0, "not re-delivered");
}

/* ================================================
 * Test 5: Get inbox for a probe
 * ================================================ */
static void test_inbox(void) {
    printf("Test: Probe inbox retrieval\n");

    comm_system_t cs;
    comm_init(&cs);

    probe_t bob = make_probe(1, 0, 0, 0, 5);
    probe_uid_t child_id = {0, 2};
    probe_uid_t other_id = {0, 3};
    vec3_t near = pos(1, 0, 0);

    /* Send two messages to child, one to other */
    comm_send_targeted(&cs, &bob, child_id, near, "msg1 for child", 100);
    comm_send_targeted(&cs, &bob, child_id, near, "msg2 for child", 100);
    comm_send_targeted(&cs, &bob, other_id, near, "msg for other", 100);

    /* Deliver all */
    comm_tick_deliver(&cs, 100 + 365 + 1);

    /* Check child's inbox */
    message_t inbox[10];
    int count = comm_get_inbox(&cs, child_id, inbox, 10);
    ASSERT_EQ_INT(count, 2, "child has 2 messages");

    /* Check other's inbox */
    count = comm_get_inbox(&cs, other_id, inbox, 10);
    ASSERT_EQ_INT(count, 1, "other has 1 message");
}

/* ================================================
 * Test 6: Out of range → rejected
 * ================================================ */
static void test_out_of_range(void) {
    printf("Test: Message to probe beyond comm range rejected\n");

    comm_system_t cs;
    comm_init(&cs);

    /* Tech level 1: range = 5 + 5*1 = 10 ly */
    probe_t bob = make_probe(1, 0, 0, 0, 1);
    vec3_t far_away = pos(100, 0, 0);  /* 100 ly, way beyond range */
    probe_uid_t target = {0, 2};

    int ret = comm_send_targeted(&cs, &bob, target, far_away,
                                 "too far", 1000);
    ASSERT_EQ_INT(ret, -1, "send rejected: out of range");
    ASSERT_EQ_INT(cs.count, 0, "no message queued");
}

/* ================================================
 * Test 7: Insufficient energy → rejected
 * ================================================ */
static void test_insufficient_energy(void) {
    printf("Test: Message rejected when insufficient energy\n");

    comm_system_t cs;
    comm_init(&cs);

    probe_t bob = make_probe(1, 0, 0, 0, 5);
    bob.energy_joules = 1.0;  /* barely any energy */
    vec3_t near = pos(1, 0, 0);
    probe_uid_t target = {0, 2};

    int ret = comm_send_targeted(&cs, &bob, target, near, "no energy", 1000);
    ASSERT_EQ_INT(ret, -1, "send rejected: no energy");
    ASSERT_EQ_INT(cs.count, 0, "no message queued");
}

/* ================================================
 * Test 8: Broadcast message to all probes in range
 * ================================================ */
static void test_broadcast(void) {
    printf("Test: Broadcast to all probes in range\n");

    comm_system_t cs;
    comm_init(&cs);

    /* Bob at origin with comm level 3: range = 5 + 5*3 = 20 ly */
    probe_t probes[4];
    probes[0] = make_probe(1, 0, 0, 0, 3);   /* Bob = sender */
    snprintf(probes[0].name, MAX_NAME, "Bob");
    probes[0].energy_joules = 100000.0;

    probes[1] = make_probe(2, 5, 0, 0, 1);   /* 5 ly away — in range */
    probes[2] = make_probe(3, 15, 0, 0, 1);  /* 15 ly away — in range */
    probes[3] = make_probe(4, 50, 0, 0, 1);  /* 50 ly away — OUT of range */

    int queued = comm_send_broadcast(&cs, &probes[0], probes, 4,
                                     "Hello everyone!", 1000);
    ASSERT_EQ_INT(queued, 2, "2 messages queued (2 in range, skip self + 1 out)");

    /* Check different arrival ticks */
    int found_5ly = 0, found_15ly = 0;
    for (int i = 0; i < cs.count; i++) {
        if (uid_eq(cs.messages[i].target_id, probes[1].id)) {
            found_5ly = 1;
            ASSERT_EQ_INT((int)cs.messages[i].arrival_tick, 1000 + 1825,
                          "5 ly arrival time");
        }
        if (uid_eq(cs.messages[i].target_id, probes[2].id)) {
            found_15ly = 1;
            ASSERT_EQ_INT((int)cs.messages[i].arrival_tick, 1000 + 5475,
                          "15 ly arrival time");
        }
    }
    ASSERT(found_5ly, "message to probe at 5 ly");
    ASSERT(found_15ly, "message to probe at 15 ly");

    /* Energy deducted for broadcast */
    ASSERT(probes[0].energy_joules < 100000.0, "broadcast energy deducted");
}

/* ================================================
 * Test 9: Place and detect beacons
 * ================================================ */
static void test_beacons(void) {
    printf("Test: Place beacon and detect in system\n");

    comm_system_t cs;
    comm_init(&cs);

    probe_t bob = make_probe(1, 10, 20, 30, 3);
    probe_uid_t sys_id = {0, 100};

    int ret = comm_place_beacon(&cs, &bob, sys_id,
                                "Warning: unstable star!", 5000);
    ASSERT_EQ_INT(ret, 0, "place beacon succeeds");
    ASSERT_EQ_INT(cs.beacon_count, 1, "one beacon");
    ASSERT(cs.beacons[0].active, "beacon is active");

    /* Detect beacons in the system */
    beacon_t found[10];
    int count = comm_detect_beacons(&cs, sys_id, found, 10);
    ASSERT_EQ_INT(count, 1, "1 beacon in system");
    ASSERT(strcmp(found[0].message, "Warning: unstable star!") == 0,
           "beacon message intact");
    ASSERT(uid_eq(found[0].owner_id, bob.id), "beacon owner matches");

    /* Detect in different system → empty */
    probe_uid_t other_sys = {0, 200};
    count = comm_detect_beacons(&cs, other_sys, found, 10);
    ASSERT_EQ_INT(count, 0, "no beacon in other system");
}

/* ================================================
 * Test 10: Deactivate beacon
 * ================================================ */
static void test_deactivate_beacon(void) {
    printf("Test: Deactivate beacon\n");

    comm_system_t cs;
    comm_init(&cs);

    probe_t bob = make_probe(1, 0, 0, 0, 3);
    probe_uid_t sys_id = {0, 100};

    comm_place_beacon(&cs, &bob, sys_id, "active beacon", 5000);
    ASSERT_EQ_INT(cs.beacon_count, 1, "beacon placed");

    int ret = comm_deactivate_beacon(&cs, bob.id, sys_id);
    ASSERT_EQ_INT(ret, 0, "deactivate succeeds");

    /* Should no longer be detected */
    beacon_t found[10];
    int count = comm_detect_beacons(&cs, sys_id, found, 10);
    ASSERT_EQ_INT(count, 0, "no active beacons after deactivation");
}

/* ================================================
 * Test 11: Build relay satellite
 * ================================================ */
static void test_build_relay(void) {
    printf("Test: Build relay satellite\n");

    comm_system_t cs;
    comm_init(&cs);

    probe_t bob = make_probe(1, 50, 0, 0, 3);
    probe_uid_t sys_id = {0, 100};

    int ret = comm_build_relay(&cs, &bob, sys_id, 10000);
    ASSERT_EQ_INT(ret, 0, "build relay succeeds");
    ASSERT_EQ_INT(cs.relay_count, 1, "one relay");
    ASSERT(cs.relays[0].active, "relay is active");
    ASSERT_NEAR(cs.relays[0].range_ly, RELAY_RANGE_LY, 0.01, "relay range");
}

/* ================================================
 * Test 12: Relay extends communication range
 * ================================================ */
static void test_relay_extends_range(void) {
    printf("Test: Relay satellite extends communication range\n");

    comm_system_t cs;
    comm_init(&cs);

    /* Bob at origin, tech 1: range = 10 ly.
     * Target at 25 ly — normally out of range.
     * Place relay at 8 ly — within Bob's range.
     * Relay has 20 ly range, can reach 8+20=28 ly from origin. */
    probe_t bob = make_probe(1, 0, 0, 0, 1);
    bob.energy_joules = 100000.0;

    /* Build relay at x=8 */
    probe_t relay_builder = make_probe(99, 8, 0, 0, 1);
    probe_uid_t relay_sys = {0, 300};
    comm_build_relay(&cs, &relay_builder, relay_sys, 5000);
    /* Override relay position to be at x=8 */
    cs.relays[0].position = pos(8, 0, 0);

    /* Direct range check: 25 ly > 10 ly = unreachable */
    double eff = comm_check_reachable(&cs, &bob, pos(25, 0, 0));
    ASSERT(eff > 0, "reachable via relay");

    /* Send should succeed through relay */
    probe_uid_t target = {0, 2};
    int ret = comm_send_targeted(&cs, &bob, target, pos(25, 0, 0),
                                 "via relay!", 1000);
    ASSERT_EQ_INT(ret, 0, "send succeeds via relay");

    /* Arrival tick should reflect relay path distance (8 + 17 = 25 ly) */
    if (cs.count > 0) {
        /* The delay is based on the actual path distance, not direct distance */
        uint64_t expected_delay = comm_light_delay(pos(0,0,0), pos(25,0,0));
        ASSERT_EQ_INT((int)cs.messages[0].arrival_tick, (int)(1000 + expected_delay),
                      "arrival uses actual distance via relay path");
    }
}

/* ================================================
 * Test 13: Round-trip message timing
 * ================================================ */
static void test_round_trip(void) {
    printf("Test: Round-trip message = 2x light delay\n");

    comm_system_t cs;
    comm_init(&cs);

    /* Two probes 5 ly apart */
    probe_t alice = make_probe(1, 0, 0, 0, 5);
    probe_t bob = make_probe(2, 5, 0, 0, 5);

    /* Alice → Bob at tick 0 */
    comm_send_targeted(&cs, &alice, bob.id, pos(5, 0, 0),
                       "ping", 0);
    ASSERT_EQ_INT((int)cs.messages[0].arrival_tick, 1825, "alice→bob = 1825 ticks");

    /* Deliver */
    comm_tick_deliver(&cs, 1825);

    /* Bob → Alice at tick 1825 (reply immediately on arrival) */
    comm_send_targeted(&cs, &bob, alice.id, pos(0, 0, 0),
                       "pong", 1825);
    ASSERT_EQ_INT((int)cs.messages[1].arrival_tick, 1825 + 1825,
                  "bob→alice = another 1825 ticks");

    /* Total round trip = 3650 ticks = 10 years for 5 ly distance */
    comm_tick_deliver(&cs, 3650);

    message_t inbox[10];
    int count = comm_get_inbox(&cs, alice.id, inbox, 10);
    ASSERT_EQ_INT(count, 1, "alice got reply");
    ASSERT(strcmp(inbox[0].content, "pong") == 0, "reply content");
}

/* ================================================
 * Test 14: Multiple beacons in one system
 * ================================================ */
static void test_multiple_beacons(void) {
    printf("Test: Multiple beacons in same system\n");

    comm_system_t cs;
    comm_init(&cs);

    probe_t p1 = make_probe(1, 0, 0, 0, 3);
    probe_t p2 = make_probe(2, 1, 0, 0, 3);
    probe_uid_t sys_id = {0, 100};

    comm_place_beacon(&cs, &p1, sys_id, "Beacon Alpha", 1000);
    comm_place_beacon(&cs, &p2, sys_id, "Beacon Beta", 2000);

    beacon_t found[10];
    int count = comm_detect_beacons(&cs, sys_id, found, 10);
    ASSERT_EQ_INT(count, 2, "two beacons in system");
}

/* ================================================
 * Test 15: Message content integrity
 * ================================================ */
static void test_message_content(void) {
    printf("Test: Message content preserved through transit and delivery\n");

    comm_system_t cs;
    comm_init(&cs);

    probe_t bob = make_probe(1, 0, 0, 0, 5);
    vec3_t near = pos(0.1, 0, 0);
    probe_uid_t target = {0, 2};

    const char *msg = "Discovered Class-M planet in Tau Ceti system. "
                      "Habitability index 0.87. Recommend colonization.";

    comm_send_targeted(&cs, &bob, target, near, msg, 100);

    /* Deliver */
    comm_tick_deliver(&cs, 100 + 365);

    message_t inbox[10];
    int count = comm_get_inbox(&cs, target, inbox, 10);
    ASSERT_EQ_INT(count, 1, "message delivered");
    ASSERT(strcmp(inbox[0].content, msg) == 0, "content exactly preserved");
    ASSERT(uid_eq(inbox[0].sender_id, bob.id), "sender ID preserved");
}

/* ================================================
 * Test 16: Comm system init is clean
 * ================================================ */
static void test_comm_init(void) {
    printf("Test: Comm system initializes clean\n");

    comm_system_t cs;
    comm_init(&cs);

    ASSERT_EQ_INT(cs.count, 0, "no messages");
    ASSERT_EQ_INT(cs.beacon_count, 0, "no beacons");
    ASSERT_EQ_INT(cs.relay_count, 0, "no relays");
}

/* ================================================
 * Test 17: Relay chain — two relays extend further
 * ================================================ */
static void test_relay_chain(void) {
    printf("Test: Relay chain extends range further\n");

    comm_system_t cs;
    comm_init(&cs);

    /* Bob at origin, tech 1: range = 10 ly.
     * Relay A at 8 ly (within Bob's range, extends to 28 ly).
     * Relay B at 25 ly (within Relay A's range, extends to 45 ly).
     * Target at 40 ly. Without relays: unreachable.
     * With chain: Bob→A(8ly)→B(25ly)→Target(40ly). */
    probe_t bob = make_probe(1, 0, 0, 0, 1);
    bob.energy_joules = 100000.0;

    /* Build relay A at x=8 */
    probe_t builder = make_probe(90, 8, 0, 0, 1);
    comm_build_relay(&cs, &builder, (probe_uid_t){0,300}, 5000);
    cs.relays[0].position = pos(8, 0, 0);

    /* Build relay B at x=25 */
    builder = make_probe(91, 25, 0, 0, 1);
    comm_build_relay(&cs, &builder, (probe_uid_t){0,301}, 5000);
    cs.relays[1].position = pos(25, 0, 0);

    double eff = comm_check_reachable(&cs, &bob, pos(40, 0, 0));
    ASSERT(eff > 0, "40 ly reachable via relay chain");

    /* Still can't reach 60 ly (chain maxes at 25+20=45) */
    eff = comm_check_reachable(&cs, &bob, pos(60, 0, 0));
    ASSERT(eff < 0, "60 ly NOT reachable even with chain");
}

/* ================================================
 * Entry point
 * ================================================ */
int main(void) {
    printf("=== Phase 8: Communication Tests ===\n\n");

    test_comm_range();
    test_light_delay();
    test_targeted_message();
    test_message_delivery();
    test_inbox();
    test_out_of_range();
    test_insufficient_energy();
    test_broadcast();
    test_beacons();
    test_deactivate_beacon();
    test_build_relay();
    test_relay_extends_range();
    test_round_trip();
    test_multiple_beacons();
    test_message_content();
    test_comm_init();
    test_relay_chain();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
