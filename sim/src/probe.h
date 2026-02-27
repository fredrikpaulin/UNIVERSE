/*
 * probe.h — Probe state management and action execution
 *
 * API defined by Phase 2 tests. Implementation in probe.c.
 */
#ifndef PROBE_H
#define PROBE_H

#include "universe.h"

/* ---- Action types ---- */

typedef enum {
    ACT_NAVIGATE_TO_BODY,   /* Move to a planet/moon within the current system */
    ACT_ENTER_ORBIT,        /* Enter orbit around a body */
    ACT_LAND,               /* Land on a body surface */
    ACT_LAUNCH,             /* Launch from surface back to orbit */
    ACT_SURVEY,             /* Survey a body (progressive levels 0-4) */
    ACT_MINE,               /* Extract a resource from the body we're landed on */
    ACT_WAIT,               /* Do nothing this tick */
    ACT_REPAIR,             /* Self-repair hull */
    ACT_TRAVEL_TO_SYSTEM,   /* Initiate interstellar travel to another system */
    ACT_REPLICATE,          /* Begin self-replication (requires resources) */
    ACT_SEND_MESSAGE,       /* Send a message to another probe */
    ACT_PLACE_BEACON,       /* Place a beacon in current system */
    ACT_BUILD_STRUCTURE,    /* Start building a structure */
    ACT_TRADE,              /* Send resources to another probe */
    ACT_CLAIM_SYSTEM,       /* Claim current system as territory */
    ACT_REVOKE_CLAIM,       /* Revoke own claim on current system */
    ACT_PROPOSE,            /* Submit a proposal for voting */
    ACT_VOTE,               /* Vote on an active proposal */
    ACT_RESEARCH,           /* Research a tech domain */
    ACT_SHARE_TECH,         /* Share tech with another probe */
    ACT_COUNT
} action_type_t;

/* ---- Action input ---- */

typedef struct {
    action_type_t  type;
    probe_uid_t    target_body;      /* For navigate/orbit/land/survey */
    probe_uid_t    target_system;    /* For travel_to_system */
    sector_coord_t target_sector;    /* For travel_to_system */
    probe_uid_t    target_probe;     /* For send_message, trade */
    resource_t     target_resource;  /* For mine, trade */
    int            survey_level;     /* 0-4 for survey */
    double         amount;           /* For trade */
    int            structure_type;   /* For build_structure (structure_type_t) */
    char           message[256];     /* For send_message, place_beacon, propose */
    int            proposal_idx;    /* For vote */
    bool           vote_favor;      /* For vote */
    int            research_domain; /* For research, share_tech (tech_domain_t) */
} action_t;

/* ---- Action result ---- */

typedef struct {
    bool  success;      /* Was the action accepted? */
    bool  completed;    /* Did the action finish this tick? (false = in progress) */
    char  error[128];   /* Error message if !success */
} action_result_t;

/* ---- Probe lifecycle ---- */

/* Initialize a probe with Bob's default config (from spec section 11).
 * Returns 0 on success. */
int probe_init_bob(probe_t *probe);

/* Execute one action for one tick. Validates the action against current
 * probe state and the system context. Mutates probe and possibly system
 * (e.g. survey marks, resource depletion). */
action_result_t probe_execute_action(probe_t *probe, const action_t *action,
                                      system_t *sys);

/* Tick the probe's energy system: fusion reactor produces energy from fuel. */
void probe_tick_energy(probe_t *probe);

/* ---- Persistence ---- */

struct persist_t_; /* forward declare to avoid circular include */
typedef struct { void *db; } persist_t_fwd; /* matches persist_t layout */

/* Save probe state to database. */
int persist_save_probe(void *persist, const probe_t *probe);

/* Load probe state by ID. Returns 0 on success, -1 if not found. */
int persist_load_probe(void *persist, probe_uid_t id, probe_t *probe);

#endif
