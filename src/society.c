/*
 * society.c — Phase 10: Multi-Probe Society
 *
 * Relationships, resource trading, territory claims, shared construction,
 * voting/council mechanics, tech sharing.
 */

#include "society.h"
#include "generate.h"
#include <string.h>
#include <math.h>

/* ---- Structure specs ---- */

static const structure_spec_t SPECS[STRUCT_TYPE_COUNT] = {
    [STRUCT_MINING_STATION]  = { 50000.0,  20000.0,  100, "Mining Station" },
    [STRUCT_RELAY_SATELLITE] = { 10000.0,  15000.0,   50, "Relay Satellite" },
    [STRUCT_OBSERVATORY]     = { 20000.0,  30000.0,   80, "Observatory" },
    [STRUCT_HABITAT]         = { 80000.0,  50000.0,  300, "Habitat" },
    [STRUCT_SHIPYARD]        = {100000.0,  60000.0,  400, "Shipyard" },
    [STRUCT_FACTORY]         = { 60000.0,  40000.0,  200, "Factory" },
};

/* Inter-system trade delay in ticks */
#define TRADE_TRANSIT_TICKS 100

/* ---- Init ---- */

void society_init(society_t *soc) {
    memset(soc, 0, sizeof(*soc));
}

const structure_spec_t *structure_get_spec(structure_type_t type) {
    if (type < 0 || type >= STRUCT_TYPE_COUNT) return NULL;
    return &SPECS[type];
}

/* ---- Relationship helpers ---- */

/* Find existing relationship index in probe a toward b, or -1 */
static int find_relationship(const probe_t *a, probe_uid_t b_id) {
    for (int i = 0; i < a->relationship_count; i++) {
        if (uid_eq(a->relationships[i].other_id, b_id))
            return i;
    }
    return -1;
}

/* Find or create relationship slot */
static relationship_t *get_or_create_rel(probe_t *a, probe_uid_t b_id) {
    int idx = find_relationship(a, b_id);
    if (idx >= 0) return &a->relationships[idx];
    if (a->relationship_count >= MAX_RELATIONSHIPS) return NULL;
    relationship_t *r = &a->relationships[a->relationship_count++];
    memset(r, 0, sizeof(*r));
    r->other_id = b_id;
    r->trust = 0.0f;
    r->disposition = 2;  /* neutral */
    return r;
}

static float clampf(float v, float lo, float hi) {
    if (v < lo) return lo;
    if (v > hi) return hi;
    return v;
}

/* ---- Relationships ---- */

void society_update_trust(probe_t *a, probe_t *b, float delta) {
    /* Update a→b */
    relationship_t *ra = get_or_create_rel(a, b->id);
    if (ra) {
        ra->trust = clampf(ra->trust + delta, -1.0f, 1.0f);
        /* Update disposition based on trust */
        if (ra->trust > 0.5f) ra->disposition = 1;       /* friendly */
        else if (ra->trust > 0.2f) ra->disposition = 2;  /* neutral-positive */
        else if (ra->trust > -0.2f) ra->disposition = 2;  /* neutral */
        else if (ra->trust > -0.5f) ra->disposition = 3;  /* wary */
        else ra->disposition = 4;                          /* hostile */
    }

    /* Update b→a (symmetric for now) */
    relationship_t *rb = get_or_create_rel(b, a->id);
    if (rb) {
        rb->trust = clampf(rb->trust + delta, -1.0f, 1.0f);
        if (rb->trust > 0.5f) rb->disposition = 1;
        else if (rb->trust > 0.2f) rb->disposition = 2;
        else if (rb->trust > -0.2f) rb->disposition = 2;
        else if (rb->trust > -0.5f) rb->disposition = 3;
        else rb->disposition = 4;
    }
}

float society_get_trust(const probe_t *a, probe_uid_t b_id) {
    int idx = find_relationship(a, b_id);
    if (idx < 0) return 0.0f;
    return a->relationships[idx].trust;
}

uint8_t society_get_disposition(const probe_t *a, probe_uid_t b_id) {
    int idx = find_relationship(a, b_id);
    if (idx < 0) return 2;  /* neutral */
    return a->relationships[idx].disposition;
}

/* ---- Resource trading ---- */

int society_trade_send(society_t *soc, probe_t *sender, probe_t *receiver,
                       resource_t resource, double amount,
                       bool same_system, uint64_t current_tick) {
    if (soc->trade_count >= MAX_TRADES) return -1;
    if (sender->resources[resource] < amount) return -1;

    /* Deduct from sender immediately */
    sender->resources[resource] -= amount;

    trade_t *t = &soc->trades[soc->trade_count++];
    t->sender_id = sender->id;
    t->receiver_id = receiver->id;
    t->resource = resource;
    t->amount = amount;
    t->status = same_system ? TRADE_IN_TRANSIT : TRADE_IN_TRANSIT;
    t->sent_tick = current_tick;
    t->same_system = same_system;
    t->arrival_tick = same_system ? current_tick : current_tick + TRADE_TRANSIT_TICKS;

    return 0;
}

int society_trade_tick(society_t *soc, probe_t *probes, int probe_count,
                       uint64_t current_tick) {
    int delivered = 0;
    for (int i = 0; i < soc->trade_count; i++) {
        trade_t *t = &soc->trades[i];
        if (t->status != TRADE_IN_TRANSIT) continue;
        if (current_tick < t->arrival_tick) continue;

        /* Find receiver probe */
        for (int p = 0; p < probe_count; p++) {
            if (uid_eq(probes[p].id, t->receiver_id)) {
                probes[p].resources[t->resource] += t->amount;
                t->status = TRADE_DELIVERED;
                delivered++;
                break;
            }
        }
    }
    return delivered;
}

/* ---- Territory claims ---- */

int society_claim_system(society_t *soc, probe_uid_t claimer_id,
                         probe_uid_t system_id, uint64_t tick) {
    /* Check if already claimed */
    for (int i = 0; i < soc->claim_count; i++) {
        if (soc->claims[i].active && uid_eq(soc->claims[i].system_id, system_id))
            return -1;
    }
    if (soc->claim_count >= MAX_CLAIMS) return -1;

    claim_t *c = &soc->claims[soc->claim_count++];
    c->claimer_id = claimer_id;
    c->system_id = system_id;
    c->claimed_tick = tick;
    c->active = true;
    return 0;
}

probe_uid_t society_get_claim(const society_t *soc, probe_uid_t system_id) {
    for (int i = 0; i < soc->claim_count; i++) {
        if (soc->claims[i].active && uid_eq(soc->claims[i].system_id, system_id))
            return soc->claims[i].claimer_id;
    }
    return uid_null();
}

int society_revoke_claim(society_t *soc, probe_uid_t claimer_id,
                         probe_uid_t system_id) {
    for (int i = 0; i < soc->claim_count; i++) {
        if (soc->claims[i].active &&
            uid_eq(soc->claims[i].claimer_id, claimer_id) &&
            uid_eq(soc->claims[i].system_id, system_id)) {
            soc->claims[i].active = false;
            return 0;
        }
    }
    return -1;
}

bool society_is_claimed_by_other(const society_t *soc, probe_uid_t system_id,
                                  probe_uid_t probe_id) {
    for (int i = 0; i < soc->claim_count; i++) {
        if (soc->claims[i].active &&
            uid_eq(soc->claims[i].system_id, system_id) &&
            !uid_eq(soc->claims[i].claimer_id, probe_id))
            return true;
    }
    return false;
}

/* ---- Shared construction ---- */

float society_build_speed_mult(int builder_count) {
    if (builder_count <= 0) return 0.0f;
    if (builder_count == 1) return 1.0f;
    /* Diminishing returns: sqrt-based scaling */
    return 1.0f + 0.6f * (float)(builder_count - 1);
}

int society_build_start(society_t *soc, probe_t *builder,
                        structure_type_t type, probe_uid_t system_id,
                        uint64_t current_tick, rng_t *rng) {
    if (soc->structure_count >= MAX_STRUCTURES) return -1;
    const structure_spec_t *spec = structure_get_spec(type);
    if (!spec) return -1;

    int idx = soc->structure_count++;
    structure_t *s = &soc->structures[idx];
    memset(s, 0, sizeof(*s));
    s->id = generate_uid(rng);
    s->type = type;
    s->system_id = system_id;
    s->builder_ids[0] = builder->id;
    s->builder_count = 1;
    s->build_ticks_total = spec->base_ticks;
    s->build_ticks_elapsed = 0;
    s->complete = false;
    s->active = false;
    s->started_tick = current_tick;

    return idx;
}

int society_build_collaborate(society_t *soc, int structure_idx,
                              probe_t *collaborator) {
    if (structure_idx < 0 || structure_idx >= soc->structure_count) return -1;
    structure_t *s = &soc->structures[structure_idx];
    if (s->complete) return -1;
    if (s->builder_count >= 4) return -1;

    s->builder_ids[s->builder_count++] = collaborator->id;
    return 0;
}

int society_build_tick(society_t *soc, uint64_t current_tick) {
    int completed = 0;
    for (int i = 0; i < soc->structure_count; i++) {
        structure_t *s = &soc->structures[i];
        if (s->complete) continue;

        float mult = society_build_speed_mult(s->builder_count);
        /* Progress = mult ticks worth per real tick.
         * We track elapsed, and complete when elapsed * mult >= total */
        s->build_ticks_elapsed++;

        if ((float)s->build_ticks_elapsed * mult >= (float)s->build_ticks_total) {
            s->complete = true;
            s->active = true;
            s->completed_tick = current_tick;
            completed++;
        }
    }
    return completed;
}

/* ---- Voting ---- */

int society_propose(society_t *soc, probe_uid_t proposer_id,
                    const char *text, uint64_t current_tick,
                    uint64_t deadline_tick) {
    if (soc->proposal_count >= MAX_PROPOSALS) return -1;

    int idx = soc->proposal_count++;
    proposal_t *p = &soc->proposals[idx];
    memset(p, 0, sizeof(*p));
    p->proposer_id = proposer_id;
    strncpy(p->text, text, MAX_PROPOSAL_TEXT - 1);
    p->proposed_tick = current_tick;
    p->deadline_tick = deadline_tick;
    p->status = VOTE_OPEN;

    return idx;
}

int society_vote(society_t *soc, int proposal_idx,
                 probe_uid_t voter_id, bool in_favor, uint64_t tick) {
    if (proposal_idx < 0 || proposal_idx >= soc->proposal_count) return -1;
    proposal_t *p = &soc->proposals[proposal_idx];
    if (p->status != VOTE_OPEN) return -1;
    if (p->vote_count >= MAX_VOTES_PER) return -1;

    /* Check for duplicate vote */
    for (int i = 0; i < p->vote_count; i++) {
        if (uid_eq(p->votes[i].voter_id, voter_id)) return -1;
    }

    vote_t *v = &p->votes[p->vote_count++];
    v->voter_id = voter_id;
    v->in_favor = in_favor;
    v->vote_tick = tick;

    if (in_favor) p->votes_for++;
    else p->votes_against++;

    return 0;
}

int society_resolve_votes(society_t *soc, uint64_t current_tick) {
    int resolved = 0;
    for (int i = 0; i < soc->proposal_count; i++) {
        proposal_t *p = &soc->proposals[i];
        if (p->status != VOTE_OPEN) continue;
        if (current_tick < p->deadline_tick) continue;

        p->status = VOTE_RESOLVED;
        p->result = (p->votes_for > p->votes_against);
        resolved++;
    }
    return resolved;
}

/* ---- Tech sharing ---- */

int society_share_tech(probe_t *sender, probe_t *receiver,
                       tech_domain_t domain) {
    if (domain < 0 || domain >= TECH_COUNT) return -1;
    if (sender->tech_levels[domain] <= receiver->tech_levels[domain]) return -1;

    receiver->tech_levels[domain] = sender->tech_levels[domain];
    return (int)receiver->tech_levels[domain];
}

uint32_t society_shared_research_ticks(uint32_t normal_ticks) {
    return (uint32_t)((float)normal_ticks * TECH_SHARE_DISCOUNT);
}
