/*
 * agent_ipc.h — Agent IPC protocol, observation/action serialization,
 *               fallback agent, and agent routing.
 *
 * API defined by Phase 4 tests. Implementation in agent_ipc.c.
 */
#ifndef AGENT_IPC_H
#define AGENT_IPC_H

#include "universe.h"
#include "probe.h"

/* ---- Observation serialization ---- */

/* Serialize probe state + current system into JSON observation.
 * Returns bytes written (excluding NUL), or -1 on error. */
int obs_serialize(const probe_t *probe, const system_t *sys,
                  char *buf, int buf_size);

/* ---- Action parsing ---- */

/* Parse JSON action string into action_t.
 * Returns 0 on success, -1 on parse error or unknown action. */
int action_parse(const char *json, action_t *out);

/* ---- Action result serialization ---- */

/* Serialize action_result_t to JSON.
 * Returns bytes written (excluding NUL), or -1 on error. */
int result_serialize(const action_result_t *res, char *buf, int buf_size);

/* ---- Name ↔ enum lookups ---- */

resource_t     resource_from_name(const char *name);
const char    *resource_to_name(resource_t r);
action_type_t  action_type_from_name(const char *name);
const char    *action_type_to_name(action_type_t t);

/* ---- Fallback agent ---- */

/* Decide an action for an uncontrolled probe. Simple survive logic:
 * repair if damaged, wait otherwise. */
action_t fallback_agent_decide(const probe_t *probe);

/* ---- Protocol framing (newline-delimited JSON) ---- */

/* Frame: append newline. Returns total length including newline. */
int protocol_frame(const char *msg, char *out, int out_size);

/* Unframe: extract message up to first newline.
 * Returns message length (without newline), or -1 if no newline found. */
int protocol_unframe(const char *buf, int buf_len, char *out, int out_size);

/* ---- Agent router ---- */

#define MAX_AGENTS 64

typedef struct {
    probe_uid_t probe_id;
    int   fd;        /* socket file descriptor, -1 = empty slot */
} agent_slot_t;

typedef struct {
    agent_slot_t slots[MAX_AGENTS];
    int          count;
} agent_router_t;

void agent_router_init(agent_router_t *r);
void agent_router_destroy(agent_router_t *r);
int  agent_router_register(agent_router_t *r, probe_uid_t probe_id, int fd);
void agent_router_unregister(agent_router_t *r, probe_uid_t probe_id);
int  agent_router_lookup(const agent_router_t *r, probe_uid_t probe_id);

#endif
