#ifndef SOCIETY_H
#define SOCIETY_H

#include "universe.h"
#include "rng.h"

/* ---- Constants ---- */

#define MAX_CLAIMS        512
#define MAX_STRUCTURES    256
#define MAX_TRADES        256
#define MAX_PROPOSALS     128
#define MAX_VOTES_PER     16
#define MAX_PROPOSAL_TEXT 256

/* Trust deltas */
#define TRUST_TRADE_POSITIVE   0.05f
#define TRUST_SHARED_DISCOVERY 0.03f
#define TRUST_TECH_SHARE       0.08f
#define TRUST_COLLAB_BUILD     0.06f
#define TRUST_CLAIM_VIOLATION -0.10f
#define TRUST_DISAGREEMENT    -0.05f

/* Tech sharing: recipient pays this fraction of normal research ticks */
#define TECH_SHARE_DISCOUNT 0.4f

/* Structure build costs (total resource kg) and ticks */
typedef enum {
    STRUCT_MINING_STATION,
    STRUCT_RELAY_SATELLITE,
    STRUCT_OBSERVATORY,
    STRUCT_HABITAT,
    STRUCT_SHIPYARD,
    STRUCT_FACTORY,
    STRUCT_TYPE_COUNT
} structure_type_t;

/* ---- Structures (built things) ---- */

typedef struct {
    probe_uid_t     id;
    structure_type_t type;
    probe_uid_t     system_id;
    probe_uid_t     builder_ids[4];  /* up to 4 collaborators */
    uint8_t         builder_count;
    uint32_t        build_ticks_total;
    uint32_t        build_ticks_elapsed;
    bool            complete;
    bool            active;
    uint64_t        started_tick;
    uint64_t        completed_tick;
} structure_t;

/* Build cost per structure type: { iron_kg, silicon_kg, ticks_solo } */
typedef struct {
    double   iron_cost;
    double   silicon_cost;
    uint32_t base_ticks;
    const char *name;
} structure_spec_t;

/* ---- Territory claims ---- */

typedef struct {
    probe_uid_t  claimer_id;
    probe_uid_t  system_id;
    uint64_t     claimed_tick;
    bool         active;
} claim_t;

/* ---- Resource trade ---- */

typedef enum {
    TRADE_PENDING,
    TRADE_IN_TRANSIT,
    TRADE_DELIVERED,
    TRADE_CANCELLED
} trade_status_t;

typedef struct {
    probe_uid_t    sender_id;
    probe_uid_t    receiver_id;
    resource_t     resource;
    double         amount;
    trade_status_t status;
    uint64_t       sent_tick;
    uint64_t       arrival_tick;   /* same-system = instant, otherwise light delay */
    bool           same_system;
} trade_t;

/* ---- Voting / Proposals ---- */

typedef enum {
    VOTE_OPEN,
    VOTE_RESOLVED,
    VOTE_EXPIRED
} proposal_status_t;

typedef struct {
    probe_uid_t voter_id;
    bool        in_favor;
    uint64_t    vote_tick;
} vote_t;

typedef struct {
    probe_uid_t       proposer_id;
    char              text[MAX_PROPOSAL_TEXT];
    uint64_t          proposed_tick;
    uint64_t          deadline_tick;   /* resolve after this */
    proposal_status_t status;
    vote_t            votes[MAX_VOTES_PER];
    int               vote_count;
    int               votes_for;
    int               votes_against;
    bool              result;          /* true = passed */
} proposal_t;

/* ---- Society system ---- */

typedef struct {
    claim_t      claims[MAX_CLAIMS];
    int          claim_count;
    structure_t  structures[MAX_STRUCTURES];
    int          structure_count;
    trade_t      trades[MAX_TRADES];
    int          trade_count;
    proposal_t   proposals[MAX_PROPOSALS];
    int          proposal_count;
} society_t;

/* ---- API ---- */

/* Initialize society system */
void society_init(society_t *soc);

/* Get structure spec for a type */
const structure_spec_t *structure_get_spec(structure_type_t type);

/* ---- Relationships ---- */

/* Update trust between two probes. Finds or creates relationship.
 * delta is added to trust, clamped to [-1, 1]. */
void society_update_trust(probe_t *a, probe_t *b, float delta);

/* Get trust from a toward b. Returns 0 if no relationship. */
float society_get_trust(const probe_t *a, probe_uid_t b_id);

/* Get disposition from a toward b. */
uint8_t society_get_disposition(const probe_t *a, probe_uid_t b_id);

/* ---- Resource trading ---- */

/* Send resources from sender to receiver. If same system, instant.
 * Otherwise, arrival after a fixed delay. Returns 0 on success. */
int society_trade_send(society_t *soc, probe_t *sender, probe_t *receiver,
                       resource_t resource, double amount,
                       bool same_system, uint64_t current_tick);

/* Deliver pending trades. Returns count delivered. */
int society_trade_tick(society_t *soc, probe_t *probes, int probe_count,
                       uint64_t current_tick);

/* ---- Territory claims ---- */

/* Claim a system. Returns 0 on success, -1 if already claimed. */
int society_claim_system(society_t *soc, probe_uid_t claimer_id,
                         probe_uid_t system_id, uint64_t tick);

/* Check who claims a system. Returns claimer ID, or null if unclaimed. */
probe_uid_t society_get_claim(const society_t *soc, probe_uid_t system_id);

/* Revoke a claim. Returns 0 on success. */
int society_revoke_claim(society_t *soc, probe_uid_t claimer_id,
                         probe_uid_t system_id);

/* Check if entering a claimed system (for trust penalty). */
bool society_is_claimed_by_other(const society_t *soc, probe_uid_t system_id,
                                  probe_uid_t probe_id);

/* ---- Shared construction ---- */

/* Begin building a structure. Returns structure index, or -1 on error. */
int society_build_start(society_t *soc, probe_t *builder,
                        structure_type_t type, probe_uid_t system_id,
                        uint64_t current_tick, rng_t *rng);

/* Add a collaborator to an in-progress structure. Speeds up build.
 * Returns 0 on success. */
int society_build_collaborate(society_t *soc, int structure_idx,
                              probe_t *collaborator);

/* Tick construction progress. Returns count of structures completed. */
int society_build_tick(society_t *soc, uint64_t current_tick);

/* Get build speed multiplier for number of builders */
float society_build_speed_mult(int builder_count);

/* ---- Voting ---- */

/* Create a proposal. Returns proposal index, or -1 on error. */
int society_propose(society_t *soc, probe_uid_t proposer_id,
                    const char *text, uint64_t current_tick,
                    uint64_t deadline_tick);

/* Cast a vote on a proposal. Returns 0 on success. */
int society_vote(society_t *soc, int proposal_idx,
                 probe_uid_t voter_id, bool in_favor, uint64_t tick);

/* Resolve proposals past their deadline. Returns count resolved. */
int society_resolve_votes(society_t *soc, uint64_t current_tick);

/* ---- Tech sharing ---- */

/* Share tech from sender to receiver. Receiver advances if sender
 * has higher level. Returns new level, or -1 if no advancement. */
int society_share_tech(probe_t *sender, probe_t *receiver,
                       tech_domain_t domain);

/* Get discounted research ticks for a shared tech level. */
uint32_t society_shared_research_ticks(uint32_t normal_ticks);

#endif /* SOCIETY_H */
