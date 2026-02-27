#ifndef SCENARIO_H
#define SCENARIO_H

#include "universe.h"
#include "events.h"
#include "rng.h"

/* ---- Constants ---- */

#define MAX_INJECTED_EVENTS  64
#define MAX_SNAPSHOT_TAG     64
#define MAX_CONFIG_KEY       64
#define MAX_CONFIG_VAL      128
#define MAX_CONFIG_ENTRIES   64
#define MAX_METRICS_HISTORY 4096
#define MAX_REPLAY_EVENTS   4096

/* ---- Scenario injection ---- */

typedef struct {
    event_type_t type;
    int          subtype;
    char         description[256];
    float        severity;        /* 0 = auto, >0 = forced */
    probe_uid_t  target_probe_id; /* null = all probes */
    bool         pending;
} injected_event_t;

typedef struct {
    injected_event_t events[MAX_INJECTED_EVENTS];
    int              count;
} injection_queue_t;

/* Initialize injection queue */
void inject_init(injection_queue_t *q);

/* Queue an event for injection on next tick.
 * Returns 0 on success, -1 if queue full. */
int inject_event(injection_queue_t *q, event_type_t type, int subtype,
                 const char *description, float severity,
                 probe_uid_t target_probe_id);

/* Parse a JSON event string into an injection.
 * Format: {"type":"crisis","subtype":0,"description":"...","severity":0.8}
 * Returns 0 on success. */
int inject_parse_json(injection_queue_t *q, const char *json);

/* Flush pending injections into the event system.
 * Returns count of events injected. */
int inject_flush(injection_queue_t *q, event_system_t *es,
                 probe_t *probes, int probe_count,
                 const system_t *sys, uint64_t tick, rng_t *rng);

/* ---- Metrics ---- */

typedef struct {
    uint64_t tick;
    uint32_t systems_explored;
    uint32_t probes_spawned;
    double   total_resources_mined;
    double   total_resources_spent;
    uint64_t longest_survival_ticks;
    double   avg_tech_level;
    uint32_t total_discoveries;
    uint32_t total_hazards_survived;
    uint32_t total_civs_found;
    float    avg_trust;         /* average inter-probe trust */
    uint32_t structures_built;
} metrics_snapshot_t;

typedef struct {
    metrics_snapshot_t history[MAX_METRICS_HISTORY];
    int                count;
    int                sample_interval;  /* record every N ticks */
} metrics_system_t;

/* Initialize metrics */
void metrics_init(metrics_system_t *ms, int sample_interval);

/* Compute and record metrics for current tick.
 * Only records if tick aligns with sample_interval. */
void metrics_record(metrics_system_t *ms, const universe_t *uni,
                    const event_system_t *es, uint64_t tick);

/* Get latest metrics snapshot, or NULL if none. */
const metrics_snapshot_t *metrics_latest(const metrics_system_t *ms);

/* Get metrics at a specific index. */
const metrics_snapshot_t *metrics_at(const metrics_system_t *ms, int index);

/* Compute average tech level across all active probes. */
double metrics_avg_tech(const universe_t *uni);

/* Compute average inter-probe trust. */
float metrics_avg_trust(const universe_t *uni);

/* Count systems explored (visited) across all probes. */
uint32_t metrics_systems_explored(const universe_t *uni);

/* ---- Snapshot / Rollback ---- */

typedef struct {
    char     tag[MAX_SNAPSHOT_TAG];
    uint64_t tick;
    uint64_t seed;
    uint32_t probe_count;
    probe_t  probes[MAX_PROBES];
    bool     valid;
} snapshot_t;

/* Take a snapshot of the universe state. */
void snapshot_take(snapshot_t *snap, const universe_t *uni, const char *tag);

/* Restore universe from snapshot.
 * Returns 0 on success, -1 if snapshot invalid. */
int snapshot_restore(const snapshot_t *snap, universe_t *uni);

/* Check if two snapshots match (for rollback verification). */
bool snapshot_matches(const snapshot_t *a, const snapshot_t *b);

/* ---- Universe forking ---- */

/* Fork: copy snapshot into a new universe with modified seed.
 * Returns 0 on success. */
int universe_fork(const snapshot_t *snap, universe_t *forked, uint64_t new_seed);

/* ---- Configuration ---- */

typedef struct {
    char key[MAX_CONFIG_KEY];
    char value[MAX_CONFIG_VAL];
} config_entry_t;

typedef struct {
    config_entry_t entries[MAX_CONFIG_ENTRIES];
    int            count;
} config_t;

/* Initialize config with defaults. */
void config_init(config_t *cfg);

/* Parse a JSON config string.
 * Format: {"event_freq_discovery":0.01,"mutation_rate":0.15,...}
 * Returns count of entries parsed. */
int config_parse_json(config_t *cfg, const char *json);

/* Get a config value by key. Returns NULL if not found. */
const char *config_get(const config_t *cfg, const char *key);

/* Get a config value as double. Returns default_val if not found. */
double config_get_double(const config_t *cfg, const char *key, double default_val);

/* Set a config value. */
int config_set(config_t *cfg, const char *key, const char *value);

/* ---- Replay ---- */

typedef struct {
    uint64_t     from_tick;
    uint64_t     to_tick;
    sim_event_t  events[MAX_REPLAY_EVENTS];
    int          event_count;
    uint64_t     current_tick;
    bool         active;
} replay_t;

/* Initialize replay from event log. */
void replay_init(replay_t *rep, const event_system_t *es,
                 uint64_t from_tick, uint64_t to_tick);

/* Step replay forward one tick. Returns events for this tick. */
int replay_step(replay_t *rep, sim_event_t *out, int max_out);

/* Check if replay is complete. */
bool replay_done(const replay_t *rep);

#endif /* SCENARIO_H */
