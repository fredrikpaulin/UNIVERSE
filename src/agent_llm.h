#ifndef AGENT_LLM_H
#define AGENT_LLM_H

#include "universe.h"
#include "probe.h"
#include "personality.h"

/* ---- Constants ---- */

#define LLM_MAX_PROMPT       16384
#define LLM_MAX_RESPONSE      4096
#define LLM_MAX_MONOLOGUE     1024
#define LLM_MAX_SUMMARY       2048
#define LLM_MAX_CONTEXT       8192
#define LLM_MAX_ACTIONS         8

/* Default deliberation: call LLM every N ticks */
#define LLM_DEFAULT_DELIBERATION_INTERVAL 10

/* ---- Prompt building ---- */

/* Build the system prompt for a probe: personality, quirks, values,
 * earth memories, current goals. Returns bytes written. */
int llm_build_system_prompt(const probe_t *probe, char *buf, int buf_size);

/* Build the observation prompt: current state, surroundings, events.
 * Returns bytes written. */
int llm_build_observation(const probe_t *probe, const system_t *sys,
                          const char *recent_events, uint64_t tick,
                          char *buf, int buf_size);

/* Build a memory context block: top N most vivid memories +
 * rolling summary. Returns bytes written. */
int llm_build_memory_context(const probe_t *probe, const char *rolling_summary,
                             int max_memories, char *buf, int buf_size);

/* Build relationship context: known probes, trust, disposition.
 * Returns bytes written. */
int llm_build_relationship_context(const probe_t *probe, char *buf, int buf_size);

/* ---- Response parsing ---- */

/* Parse an LLM response into actions and monologue.
 * Expects JSON: { "actions": [...], "monologue": "...", "reasoning": "..." }
 * Returns number of actions parsed, or -1 on error. */
int llm_parse_response(const char *response, action_t *actions, int max_actions,
                       char *monologue, int monologue_size);

/* ---- Context management ---- */

typedef struct {
    char  rolling_summary[LLM_MAX_SUMMARY];
    int   events_since_summary;
    int   summary_interval;     /* summarize every N events */
} llm_context_t;

/* Initialize context manager. */
void llm_context_init(llm_context_t *ctx, int summary_interval);

/* Append an event description to the rolling context.
 * When events_since_summary exceeds interval, compresses into summary. */
void llm_context_append_event(llm_context_t *ctx, const char *event_desc);

/* Get the current summary. */
const char *llm_context_get_summary(const llm_context_t *ctx);

/* ---- Cost tracking ---- */

typedef struct {
    uint64_t total_calls;
    uint64_t total_input_tokens;
    uint64_t total_output_tokens;
    double   total_cost_usd;
    double   cost_per_token_input;   /* configurable rate */
    double   cost_per_token_output;
} llm_cost_tracker_t;

/* Initialize cost tracker with token rates. */
void llm_cost_init(llm_cost_tracker_t *ct, double input_rate, double output_rate);

/* Record a single LLM call. */
void llm_cost_record(llm_cost_tracker_t *ct, int input_tokens, int output_tokens);

/* Get average cost per call. */
double llm_cost_avg_per_call(const llm_cost_tracker_t *ct);

/* Get average tokens per call. */
double llm_cost_avg_tokens(const llm_cost_tracker_t *ct);

/* ---- Deliberation throttle ---- */

typedef struct {
    int      interval;           /* call LLM every N ticks */
    uint64_t last_deliberation;  /* tick of last LLM call */
    bool     force_next;         /* force deliberation on next check */
} llm_deliberation_t;

/* Initialize deliberation throttle. */
void llm_delib_init(llm_deliberation_t *d, int interval);

/* Check if probe should deliberate this tick. */
bool llm_delib_should_call(const llm_deliberation_t *d, uint64_t current_tick);

/* Record that deliberation happened. */
void llm_delib_record(llm_deliberation_t *d, uint64_t tick);

/* Force next deliberation (e.g., after a significant event). */
void llm_delib_force(llm_deliberation_t *d);

/* ---- Decision logging ---- */

typedef struct {
    uint64_t   tick;
    probe_uid_t probe_id;
    char       observation_hash[17];  /* short hash of observation */
    action_t   action;
    char       monologue[LLM_MAX_MONOLOGUE];
    int        input_tokens;
    int        output_tokens;
} llm_decision_log_entry_t;

#define LLM_MAX_LOG 1024

typedef struct {
    llm_decision_log_entry_t entries[LLM_MAX_LOG];
    int count;
} llm_decision_log_t;

/* Initialize decision log. */
void llm_log_init(llm_decision_log_t *log);

/* Record a decision. */
void llm_log_record(llm_decision_log_t *log, uint64_t tick,
                    probe_uid_t probe_id, const action_t *action,
                    const char *monologue, int input_tokens, int output_tokens);

/* Get decisions for a probe. Returns count. */
int llm_log_get_for_probe(const llm_decision_log_t *log, probe_uid_t probe_id,
                          llm_decision_log_entry_t *out, int max_out);

/* ---- Personality-influenced prompt hints ---- */

/* Generate personality flavor text for the system prompt.
 * e.g., "You are deeply curious but cautious. You value exploration."
 * Returns bytes written. */
int llm_personality_flavor(const personality_traits_t *p,
                           char *buf, int buf_size);

#endif /* AGENT_LLM_H */
