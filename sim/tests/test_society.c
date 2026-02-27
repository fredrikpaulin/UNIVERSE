/*
 * test_society.c — Phase 10: Society tests
 *
 * Tests: relationships, resource trading, territory claims, shared
 *        construction, voting, tech sharing.
 */

#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/society.h"
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

/* Helper: make a probe */
static probe_t make_probe(uint64_t id_lo, const char *name) {
    probe_t p = {0};
    p.id = (probe_uid_t){0, id_lo};
    snprintf(p.name, MAX_NAME, "%s", name);
    p.status = STATUS_ACTIVE;
    p.location_type = LOC_IN_SYSTEM;
    p.hull_integrity = 1.0f;
    p.energy_joules = 1000000.0;
    for (int r = 0; r < RES_COUNT; r++)
        p.resources[r] = 500000.0;
    for (int t = 0; t < TECH_COUNT; t++)
        p.tech_levels[t] = 3;
    return p;
}

/* ================================================
 * Test 1: Society system initialization
 * ================================================ */
static void test_init(void) {
    printf("Test: Society system initializes clean\n");

    society_t soc;
    society_init(&soc);

    ASSERT_EQ_INT(soc.claim_count, 0, "no claims");
    ASSERT_EQ_INT(soc.structure_count, 0, "no structures");
    ASSERT_EQ_INT(soc.trade_count, 0, "no trades");
    ASSERT_EQ_INT(soc.proposal_count, 0, "no proposals");
}

/* ================================================
 * Test 2: Trust updated between probes
 * ================================================ */
static void test_trust_update(void) {
    printf("Test: Trust increases after positive interaction\n");

    probe_t alice = make_probe(1, "Alice");
    probe_t bob = make_probe(2, "Bob");

    /* Initially no relationship */
    float t0 = society_get_trust(&alice, bob.id);
    ASSERT_NEAR(t0, 0.0, 0.01, "initial trust is 0");

    /* Positive interaction */
    society_update_trust(&alice, &bob, TRUST_TRADE_POSITIVE);
    float t1 = society_get_trust(&alice, bob.id);
    ASSERT(t1 > 0.0f, "trust increased after trade");
    ASSERT_NEAR(t1, TRUST_TRADE_POSITIVE, 0.01, "trust = trade delta");

    /* Negative interaction */
    society_update_trust(&alice, &bob, TRUST_CLAIM_VIOLATION);
    float t2 = society_get_trust(&alice, bob.id);
    ASSERT(t2 < t1, "trust decreased after violation");
}

/* ================================================
 * Test 3: Trust clamped to [-1, 1]
 * ================================================ */
static void test_trust_clamp(void) {
    printf("Test: Trust clamped to [-1, 1]\n");

    probe_t alice = make_probe(1, "Alice");
    probe_t bob = make_probe(2, "Bob");

    /* Push trust very high */
    for (int i = 0; i < 100; i++)
        society_update_trust(&alice, &bob, 0.1f);

    float high = society_get_trust(&alice, bob.id);
    ASSERT(high <= 1.0f, "trust capped at 1.0");

    /* Push trust very low */
    for (int i = 0; i < 300; i++)
        society_update_trust(&alice, &bob, -0.1f);

    float low = society_get_trust(&alice, bob.id);
    ASSERT(low >= -1.0f, "trust floored at -1.0");
}

/* ================================================
 * Test 4: Resource trade — same system (instant)
 * ================================================ */
static void test_trade_same_system(void) {
    printf("Test: Resource trade in same system\n");

    society_t soc;
    society_init(&soc);

    probe_t alice = make_probe(1, "Alice");
    probe_t bob = make_probe(2, "Bob");
    double alice_iron_before = alice.resources[RES_IRON];
    double bob_iron_before = bob.resources[RES_IRON];

    int ret = society_trade_send(&soc, &alice, &bob, RES_IRON, 10000.0,
                                 true, 1000);
    ASSERT_EQ_INT(ret, 0, "trade send succeeds");
    ASSERT_EQ_INT(soc.trade_count, 1, "one trade recorded");

    /* Sender loses resources immediately */
    ASSERT_NEAR(alice.resources[RES_IRON], alice_iron_before - 10000.0, 0.01,
                "alice iron deducted");

    /* Deliver */
    probe_t probes[] = {alice, bob};
    int delivered = society_trade_tick(&soc, probes, 2, 1000);
    ASSERT_EQ_INT(delivered, 1, "trade delivered instantly");
    ASSERT_NEAR(probes[1].resources[RES_IRON], bob_iron_before + 10000.0, 0.01,
                "bob iron received");
}

/* ================================================
 * Test 5: Resource trade — insufficient resources
 * ================================================ */
static void test_trade_insufficient(void) {
    printf("Test: Trade rejected with insufficient resources\n");

    society_t soc;
    society_init(&soc);

    probe_t alice = make_probe(1, "Alice");
    probe_t bob = make_probe(2, "Bob");
    alice.resources[RES_EXOTIC] = 100.0;

    int ret = society_trade_send(&soc, &alice, &bob, RES_EXOTIC, 999.0,
                                 true, 1000);
    ASSERT_EQ_INT(ret, -1, "trade rejected: insufficient");
    ASSERT_EQ_INT(soc.trade_count, 0, "no trade recorded");
}

/* ================================================
 * Test 6: Territory claim
 * ================================================ */
static void test_claim_system(void) {
    printf("Test: Claim a system\n");

    society_t soc;
    society_init(&soc);

    probe_uid_t alice_id = {0, 1};
    probe_uid_t sys_id = {0, 100};

    int ret = society_claim_system(&soc, alice_id, sys_id, 1000);
    ASSERT_EQ_INT(ret, 0, "claim succeeds");
    ASSERT_EQ_INT(soc.claim_count, 1, "one claim");

    probe_uid_t claimer = society_get_claim(&soc, sys_id);
    ASSERT(uid_eq(claimer, alice_id), "alice owns the claim");
}

/* ================================================
 * Test 7: Double claim rejected
 * ================================================ */
static void test_double_claim(void) {
    printf("Test: Can't claim already-claimed system\n");

    society_t soc;
    society_init(&soc);

    probe_uid_t alice_id = {0, 1};
    probe_uid_t bob_id = {0, 2};
    probe_uid_t sys_id = {0, 100};

    society_claim_system(&soc, alice_id, sys_id, 1000);
    int ret = society_claim_system(&soc, bob_id, sys_id, 1001);
    ASSERT_EQ_INT(ret, -1, "double claim rejected");

    /* Alice still owns it */
    probe_uid_t claimer = society_get_claim(&soc, sys_id);
    ASSERT(uid_eq(claimer, alice_id), "alice still owns claim");
}

/* ================================================
 * Test 8: Claimed-by-other detection
 * ================================================ */
static void test_claimed_by_other(void) {
    printf("Test: Detect system claimed by another probe\n");

    society_t soc;
    society_init(&soc);

    probe_uid_t alice_id = {0, 1};
    probe_uid_t bob_id = {0, 2};
    probe_uid_t sys_id = {0, 100};

    society_claim_system(&soc, alice_id, sys_id, 1000);

    ASSERT(society_is_claimed_by_other(&soc, sys_id, bob_id),
           "bob sees alice's claim");
    ASSERT(!society_is_claimed_by_other(&soc, sys_id, alice_id),
           "alice doesn't see own claim as other's");
}

/* ================================================
 * Test 9: Revoke claim
 * ================================================ */
static void test_revoke_claim(void) {
    printf("Test: Revoke territory claim\n");

    society_t soc;
    society_init(&soc);

    probe_uid_t alice_id = {0, 1};
    probe_uid_t sys_id = {0, 100};

    society_claim_system(&soc, alice_id, sys_id, 1000);
    int ret = society_revoke_claim(&soc, alice_id, sys_id);
    ASSERT_EQ_INT(ret, 0, "revoke succeeds");

    probe_uid_t claimer = society_get_claim(&soc, sys_id);
    ASSERT(uid_is_null(claimer), "system unclaimed after revoke");
}

/* ================================================
 * Test 10: Build structure solo
 * ================================================ */
static void test_build_solo(void) {
    printf("Test: Build mining station solo\n");

    society_t soc;
    society_init(&soc);
    rng_t rng;
    rng_seed(&rng, 42);

    probe_t alice = make_probe(1, "Alice");
    probe_uid_t sys_id = {0, 100};

    int idx = society_build_start(&soc, &alice, STRUCT_MINING_STATION, sys_id, 1000, &rng);
    ASSERT(idx >= 0, "build started");
    ASSERT_EQ_INT(soc.structure_count, 1, "one structure");
    ASSERT(!soc.structures[0].complete, "not complete yet");
    ASSERT_EQ_INT(soc.structures[0].builder_count, 1, "one builder");

    /* Tick until complete */
    const structure_spec_t *spec = structure_get_spec(STRUCT_MINING_STATION);
    uint32_t solo_ticks = spec->base_ticks;
    for (uint32_t t = 0; t < solo_ticks; t++) {
        society_build_tick(&soc, 1000 + t);
    }

    ASSERT(soc.structures[0].complete, "structure complete after enough ticks");
    ASSERT(soc.structures[0].active, "structure is active");
}

/* ================================================
 * Test 11: Collaborative build is faster
 * ================================================ */
static void test_build_collab(void) {
    printf("Test: Two probes build faster than one\n");

    society_t soc;
    society_init(&soc);
    rng_t rng;
    rng_seed(&rng, 42);

    probe_t alice = make_probe(1, "Alice");
    probe_t bob = make_probe(2, "Bob");
    probe_uid_t sys_id = {0, 100};

    int idx = society_build_start(&soc, &alice, STRUCT_SHIPYARD, sys_id, 1000, &rng);
    ASSERT(idx >= 0, "build started");

    int ret = society_build_collaborate(&soc, idx, &bob);
    ASSERT_EQ_INT(ret, 0, "bob joined as collaborator");
    ASSERT_EQ_INT(soc.structures[0].builder_count, 2, "two builders");

    /* Speed multiplier for 2 builders */
    float mult = society_build_speed_mult(2);
    ASSERT(mult > 1.0f, "2 builders = faster than 1");

    const structure_spec_t *spec = structure_get_spec(STRUCT_SHIPYARD);
    /* Collab ticks needed should be less than solo */
    uint32_t collab_ticks = (uint32_t)((float)spec->base_ticks / mult) + 1;
    ASSERT(collab_ticks < spec->base_ticks, "collab finishes in fewer ticks");

    for (uint32_t t = 0; t < collab_ticks + 10; t++) {
        society_build_tick(&soc, 1000 + t);
    }
    ASSERT(soc.structures[0].complete, "collab build complete");
}

/* ================================================
 * Test 12: Structure specs exist
 * ================================================ */
static void test_structure_specs(void) {
    printf("Test: Structure specs for all types\n");

    for (int i = 0; i < STRUCT_TYPE_COUNT; i++) {
        const structure_spec_t *spec = structure_get_spec((structure_type_t)i);
        ASSERT(spec != NULL, "spec exists");
        if (spec) {
            ASSERT(spec->base_ticks > 0, "has build time");
            ASSERT(spec->iron_cost > 0, "has iron cost");
            ASSERT(spec->name != NULL, "has name");
        }
    }
}

/* ================================================
 * Test 13: Voting — propose and vote
 * ================================================ */
static void test_voting(void) {
    printf("Test: Proposal and voting\n");

    society_t soc;
    society_init(&soc);

    probe_uid_t alice_id = {0, 1};
    probe_uid_t bob_id = {0, 2};
    probe_uid_t charlie_id = {0, 3};

    int idx = society_propose(&soc, alice_id,
                              "Should we terraform planet Kepler-442b?",
                              1000, 5000);
    ASSERT(idx >= 0, "proposal created");
    ASSERT_EQ_INT(soc.proposal_count, 1, "one proposal");
    ASSERT_EQ_INT((int)soc.proposals[0].status, VOTE_OPEN, "proposal is open");

    /* Cast votes */
    int ret;
    ret = society_vote(&soc, idx, alice_id, true, 1100);
    ASSERT_EQ_INT(ret, 0, "alice votes yes");
    ret = society_vote(&soc, idx, bob_id, true, 2000);
    ASSERT_EQ_INT(ret, 0, "bob votes yes");
    ret = society_vote(&soc, idx, charlie_id, false, 3000);
    ASSERT_EQ_INT(ret, 0, "charlie votes no");

    ASSERT_EQ_INT(soc.proposals[0].vote_count, 3, "3 votes cast");
    ASSERT_EQ_INT(soc.proposals[0].votes_for, 2, "2 for");
    ASSERT_EQ_INT(soc.proposals[0].votes_against, 1, "1 against");

    /* Resolve at deadline */
    int resolved = society_resolve_votes(&soc, 5001);
    ASSERT_EQ_INT(resolved, 1, "proposal resolved");
    ASSERT_EQ_INT((int)soc.proposals[0].status, VOTE_RESOLVED, "status resolved");
    ASSERT(soc.proposals[0].result, "proposal passed (2-1)");
}

/* ================================================
 * Test 14: Vote fails if majority against
 * ================================================ */
static void test_voting_fails(void) {
    printf("Test: Proposal fails with majority against\n");

    society_t soc;
    society_init(&soc);

    probe_uid_t a = {0, 1}, b = {0, 2}, c = {0, 3};

    int idx = society_propose(&soc, a, "Attack the alien colony?", 1000, 5000);
    society_vote(&soc, idx, a, true, 1100);
    society_vote(&soc, idx, b, false, 2000);
    society_vote(&soc, idx, c, false, 3000);

    society_resolve_votes(&soc, 5001);
    ASSERT(!soc.proposals[0].result, "proposal failed (1-2)");
}

/* ================================================
 * Test 15: Tech sharing
 * ================================================ */
static void test_tech_sharing(void) {
    printf("Test: Tech sharing advances receiver\n");

    probe_t alice = make_probe(1, "Alice");
    probe_t bob = make_probe(2, "Bob");

    alice.tech_levels[TECH_PROPULSION] = 7;
    bob.tech_levels[TECH_PROPULSION] = 3;

    int new_level = society_share_tech(&alice, &bob, TECH_PROPULSION);
    ASSERT_EQ_INT(new_level, 7, "bob advances to alice's level");
    ASSERT_EQ_INT(bob.tech_levels[TECH_PROPULSION], 7, "bob's tech updated");

    /* Trust should increase on both sides conceptually,
     * but we test the function return here */
}

/* ================================================
 * Test 16: Tech sharing — no advancement if already equal or higher
 * ================================================ */
static void test_tech_sharing_no_advance(void) {
    printf("Test: Tech sharing no advancement if receiver already equal\n");

    probe_t alice = make_probe(1, "Alice");
    probe_t bob = make_probe(2, "Bob");

    alice.tech_levels[TECH_SENSORS] = 3;
    bob.tech_levels[TECH_SENSORS] = 5;

    int ret = society_share_tech(&alice, &bob, TECH_SENSORS);
    ASSERT_EQ_INT(ret, -1, "no advancement (bob already higher)");
    ASSERT_EQ_INT(bob.tech_levels[TECH_SENSORS], 5, "bob unchanged");
}

/* ================================================
 * Test 17: Discounted research ticks
 * ================================================ */
static void test_shared_research_discount(void) {
    printf("Test: Shared tech has discounted research ticks\n");

    uint32_t normal = 1000;
    uint32_t discounted = society_shared_research_ticks(normal);
    ASSERT(discounted < normal, "discounted < normal");
    uint32_t expected = (uint32_t)(normal * TECH_SHARE_DISCOUNT);
    ASSERT_EQ_INT((int)discounted, (int)expected, "discount factor applied");
}

/* ================================================
 * Test 18: Trade with transit delay
 * ================================================ */
static void test_trade_transit(void) {
    printf("Test: Trade between systems has transit delay\n");

    society_t soc;
    society_init(&soc);

    probe_t alice = make_probe(1, "Alice");
    probe_t bob = make_probe(2, "Bob");
    double bob_silicon_before = bob.resources[RES_SILICON];

    /* Different system trade: 100 tick delivery delay */
    int ret = society_trade_send(&soc, &alice, &bob, RES_SILICON, 5000.0,
                                 false, 1000);
    ASSERT_EQ_INT(ret, 0, "trade sent");

    /* Not delivered yet at tick 1050 */
    probe_t probes[] = {alice, bob};
    int delivered = society_trade_tick(&soc, probes, 2, 1050);
    ASSERT_EQ_INT(delivered, 0, "not delivered yet");

    /* Delivered after arrival tick */
    delivered = society_trade_tick(&soc, probes, 2, soc.trades[0].arrival_tick + 1);
    ASSERT_EQ_INT(delivered, 1, "trade delivered after delay");
    ASSERT_NEAR(probes[1].resources[RES_SILICON], bob_silicon_before + 5000.0, 0.01,
                "bob got silicon");
}

/* ================================================
 * Test 19: Duplicate vote rejected
 * ================================================ */
static void test_duplicate_vote(void) {
    printf("Test: Same probe can't vote twice\n");

    society_t soc;
    society_init(&soc);

    probe_uid_t a = {0, 1};
    int idx = society_propose(&soc, a, "Test proposal", 1000, 5000);
    society_vote(&soc, idx, a, true, 1100);
    int ret = society_vote(&soc, idx, a, false, 1200);
    ASSERT_EQ_INT(ret, -1, "duplicate vote rejected");
    ASSERT_EQ_INT(soc.proposals[0].vote_count, 1, "still only 1 vote");
}

/* ================================================
 * Test 20: Build speed multiplier scales
 * ================================================ */
static void test_build_speed_scaling(void) {
    printf("Test: Build speed scales with collaborators\n");

    float m1 = society_build_speed_mult(1);
    float m2 = society_build_speed_mult(2);
    float m3 = society_build_speed_mult(3);
    float m4 = society_build_speed_mult(4);

    ASSERT_NEAR(m1, 1.0, 0.01, "1 builder = 1x");
    ASSERT(m2 > m1, "2 > 1");
    ASSERT(m3 > m2, "3 > 2");
    ASSERT(m4 > m3, "4 > 3");
    ASSERT(m4 <= 4.0f, "diminishing returns: 4 builders < 4x");
}

/* ================================================
 * Entry point
 * ================================================ */
int main(void) {
    printf("=== Phase 10: Society Tests ===\n\n");

    test_init();
    test_trust_update();
    test_trust_clamp();
    test_trade_same_system();
    test_trade_insufficient();
    test_claim_system();
    test_double_claim();
    test_claimed_by_other();
    test_revoke_claim();
    test_build_solo();
    test_build_collab();
    test_structure_specs();
    test_voting();
    test_voting_fails();
    test_tech_sharing();
    test_tech_sharing_no_advance();
    test_shared_research_discount();
    test_trade_transit();
    test_duplicate_vote();
    test_build_speed_scaling();

    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
