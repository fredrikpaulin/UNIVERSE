/*
 * scenario.c — Phase 12: Scenario framework implementation
 *
 * Event injection, metrics, snapshots, config, replay, forking.
 */

#include "scenario.h"
#include <string.h>
#include <stdlib.h>
#include <stdio.h>
#include <math.h>

/* ---- Injection ---- */

void inject_init(injection_queue_t *q) {
    memset(q, 0, sizeof(*q));
}

int inject_event(injection_queue_t *q, event_type_t type, int subtype,
                 const char *description, float severity,
                 probe_uid_t target_probe_id) {
    if (q->count >= MAX_INJECTED_EVENTS) return -1;
    injected_event_t *ev = &q->events[q->count++];
    ev->type = type;
    ev->subtype = subtype;
    if (description) {
        memset(ev->description, 0, sizeof(ev->description));
        strncpy(ev->description, description, sizeof(ev->description) - 1);
    }
    ev->severity = severity;
    ev->target_probe_id = target_probe_id;
    ev->pending = true;
    return 0;
}

/* Mini JSON helpers for injection */
static const char *json_find_str(const char *json, const char *key, char *out, int out_size) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    if (*p == '"') {
        p++;
        int i = 0;
        while (*p && *p != '"' && i < out_size - 1) out[i++] = *p++;
        out[i] = '\0';
        return out;
    }
    return NULL;
}

static double json_find_num(const char *json, const char *key, double def) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return def;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':') p++;
    return atof(p);
}

int inject_parse_json(injection_queue_t *q, const char *json) {
    if (q->count >= MAX_INJECTED_EVENTS) return -1;

    char type_str[64] = {0};
    char desc[256] = {0};
    json_find_str(json, "type", type_str, sizeof(type_str));
    json_find_str(json, "description", desc, sizeof(desc));
    int subtype = (int)json_find_num(json, "subtype", 0);
    float severity = (float)json_find_num(json, "severity", 0);

    event_type_t type = EVT_DISCOVERY;
    if (strcmp(type_str, "hazard") == 0) type = EVT_HAZARD;
    else if (strcmp(type_str, "anomaly") == 0) type = EVT_ANOMALY;
    else if (strcmp(type_str, "wonder") == 0) type = EVT_WONDER;
    else if (strcmp(type_str, "crisis") == 0) type = EVT_CRISIS;
    else if (strcmp(type_str, "encounter") == 0) type = EVT_ENCOUNTER;

    return inject_event(q, type, subtype, desc, severity, uid_null());
}

int inject_flush(injection_queue_t *q, event_system_t *es,
                 probe_t *probes, int probe_count,
                 const system_t *sys, uint64_t tick, rng_t *rng) {
    int flushed = 0;
    for (int i = 0; i < q->count; i++) {
        injected_event_t *ev = &q->events[i];
        if (!ev->pending) continue;

        bool targeted = !uid_eq(ev->target_probe_id, uid_null());

        for (int p = 0; p < probe_count; p++) {
            if (targeted && !uid_eq(probes[p].id, ev->target_probe_id))
                continue;

            /* Use events_generate to log + apply effects */
            events_generate(es, &probes[p], ev->type, ev->subtype,
                           sys, tick, rng);
            flushed++;
        }
        ev->pending = false;
    }
    q->count = 0;
    return flushed;
}

/* ---- Metrics ---- */

void metrics_init(metrics_system_t *ms, int sample_interval) {
    memset(ms, 0, sizeof(*ms));
    ms->sample_interval = sample_interval;
}

double metrics_avg_tech(const universe_t *uni) {
    if (uni->probe_count == 0) return 0.0;
    double total = 0.0;
    int active = 0;
    for (uint32_t i = 0; i < uni->probe_count; i++) {
        if (uni->probes[i].status != STATUS_ACTIVE) continue;
        double probe_avg = 0.0;
        for (int t = 0; t < TECH_COUNT; t++)
            probe_avg += uni->probes[i].tech_levels[t];
        probe_avg /= TECH_COUNT;
        total += probe_avg;
        active++;
    }
    return active > 0 ? total / active : 0.0;
}

float metrics_avg_trust(const universe_t *uni) {
    float total = 0.0f;
    int count = 0;
    for (uint32_t i = 0; i < uni->probe_count; i++) {
        if (uni->probes[i].status != STATUS_ACTIVE) continue;
        for (int r = 0; r < uni->probes[i].relationship_count; r++) {
            total += uni->probes[i].relationships[r].trust;
            count++;
        }
    }
    return count > 0 ? total / count : 0.0f;
}

uint32_t metrics_systems_explored(const universe_t *uni) {
    /* Count unique systems visited across all probes */
    /* Simple approach: count probes that have moved from origin */
    uint32_t count = 0;
    for (uint32_t i = 0; i < uni->probe_count; i++) {
        if (uni->probes[i].status == STATUS_ACTIVE)
            count++;  /* rough approximation */
    }
    return count;
}

void metrics_record(metrics_system_t *ms, const universe_t *uni,
                    const event_system_t *es, uint64_t tick) {
    if (ms->sample_interval > 0 && (tick % ms->sample_interval) != 0)
        return;
    if (ms->count >= MAX_METRICS_HISTORY) return;

    metrics_snapshot_t *snap = &ms->history[ms->count++];
    memset(snap, 0, sizeof(*snap));
    snap->tick = tick;

    /* Count active probes */
    uint32_t active = 0;
    for (uint32_t i = 0; i < uni->probe_count; i++)
        if (uni->probes[i].status == STATUS_ACTIVE) active++;

    snap->probes_spawned = uni->probe_count;
    snap->systems_explored = metrics_systems_explored(uni);
    snap->avg_tech_level = metrics_avg_tech(uni);
    snap->avg_trust = metrics_avg_trust(uni);

    /* Count from event log */
    uint32_t discoveries = 0, hazards = 0, civs = 0;
    for (int i = 0; i < es->count; i++) {
        if (es->events[i].type == EVT_DISCOVERY) discoveries++;
        else if (es->events[i].type == EVT_HAZARD) hazards++;
        else if (es->events[i].type == EVT_ENCOUNTER) civs++;
    }
    snap->total_discoveries = discoveries;
    snap->total_hazards_survived = hazards;
    snap->total_civs_found = civs;

    (void)active;
}

const metrics_snapshot_t *metrics_latest(const metrics_system_t *ms) {
    if (ms->count == 0) return NULL;
    return &ms->history[ms->count - 1];
}

const metrics_snapshot_t *metrics_at(const metrics_system_t *ms, int index) {
    if (index < 0 || index >= ms->count) return NULL;
    return &ms->history[index];
}

/* ---- Snapshot ---- */

void snapshot_take(snapshot_t *snap, const universe_t *uni, const char *tag) {
    memset(snap, 0, sizeof(*snap));
    strncpy(snap->tag, tag, MAX_SNAPSHOT_TAG - 1);
    snap->tick = uni->tick;
    snap->seed = uni->seed;
    snap->probe_count = uni->probe_count;
    memcpy(snap->probes, uni->probes, sizeof(probe_t) * uni->probe_count);
    snap->valid = true;
}

int snapshot_restore(const snapshot_t *snap, universe_t *uni) {
    if (!snap->valid) return -1;
    uni->tick = snap->tick;
    uni->seed = snap->seed;
    uni->probe_count = snap->probe_count;
    memcpy(uni->probes, snap->probes, sizeof(probe_t) * snap->probe_count);
    return 0;
}

bool snapshot_matches(const snapshot_t *a, const snapshot_t *b) {
    if (!a->valid || !b->valid) return false;
    if (a->tick != b->tick) return false;
    if (a->seed != b->seed) return false;
    if (a->probe_count != b->probe_count) return false;
    /* Compare probe data */
    for (uint32_t i = 0; i < a->probe_count; i++) {
        if (memcmp(&a->probes[i], &b->probes[i], sizeof(probe_t)) != 0)
            return false;
    }
    return true;
}

/* ---- Universe forking ---- */

int universe_fork(const snapshot_t *snap, universe_t *forked, uint64_t new_seed) {
    if (!snap->valid) return -1;
    memset(forked, 0, sizeof(*forked));
    forked->tick = snap->tick;
    forked->seed = new_seed;
    forked->probe_count = snap->probe_count;
    forked->running = true;
    memcpy(forked->probes, snap->probes, sizeof(probe_t) * snap->probe_count);
    return 0;
}

/* ---- Configuration ---- */

void config_init(config_t *cfg) {
    memset(cfg, 0, sizeof(*cfg));
}

const char *config_get(const config_t *cfg, const char *key) {
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0)
            return cfg->entries[i].value;
    }
    return NULL;
}

double config_get_double(const config_t *cfg, const char *key, double default_val) {
    const char *val = config_get(cfg, key);
    if (!val) return default_val;
    return atof(val);
}

int config_set(config_t *cfg, const char *key, const char *value) {
    /* Update existing */
    for (int i = 0; i < cfg->count; i++) {
        if (strcmp(cfg->entries[i].key, key) == 0) {
            strncpy(cfg->entries[i].value, value, MAX_CONFIG_VAL - 1);
            return 0;
        }
    }
    /* Add new */
    if (cfg->count >= MAX_CONFIG_ENTRIES) return -1;
    config_entry_t *e = &cfg->entries[cfg->count++];
    strncpy(e->key, key, MAX_CONFIG_KEY - 1);
    strncpy(e->value, value, MAX_CONFIG_VAL - 1);
    return 0;
}

int config_parse_json(config_t *cfg, const char *json) {
    int count = 0;
    const char *p = json;

    /* Skip opening brace */
    while (*p && *p != '{') p++;
    if (*p == '{') p++;

    while (*p) {
        /* Find key */
        while (*p && *p != '"') p++;
        if (!*p) break;
        p++; /* skip opening quote */

        char key[MAX_CONFIG_KEY] = {0};
        int ki = 0;
        while (*p && *p != '"' && ki < MAX_CONFIG_KEY - 1) key[ki++] = *p++;
        if (*p == '"') p++;

        /* Skip colon */
        while (*p && *p != ':') p++;
        if (*p == ':') p++;
        while (*p == ' ') p++;

        /* Read value (number or string) */
        char val[MAX_CONFIG_VAL] = {0};
        if (*p == '"') {
            p++;
            int vi = 0;
            while (*p && *p != '"' && vi < MAX_CONFIG_VAL - 1) val[vi++] = *p++;
            if (*p == '"') p++;
        } else {
            /* Number */
            int vi = 0;
            while (*p && *p != ',' && *p != '}' && vi < MAX_CONFIG_VAL - 1)
                val[vi++] = *p++;
        }

        if (key[0] && val[0]) {
            config_set(cfg, key, val);
            count++;
        }

        /* Skip comma */
        while (*p && *p != ',' && *p != '}') p++;
        if (*p == '}') break;
        if (*p == ',') p++;
    }

    return count;
}

/* ---- Replay ---- */

void replay_init(replay_t *rep, const event_system_t *es,
                 uint64_t from_tick, uint64_t to_tick) {
    memset(rep, 0, sizeof(*rep));
    rep->from_tick = from_tick;
    rep->to_tick = to_tick;
    rep->current_tick = from_tick;

    /* Copy events in range */
    for (int i = 0; i < es->count && rep->event_count < MAX_REPLAY_EVENTS; i++) {
        if (es->events[i].tick >= from_tick && es->events[i].tick <= to_tick) {
            rep->events[rep->event_count++] = es->events[i];
        }
    }

    rep->active = (rep->event_count > 0);
}

int replay_step(replay_t *rep, sim_event_t *out, int max_out) {
    if (!rep->active || rep->current_tick > rep->to_tick) return 0;

    int count = 0;
    for (int i = 0; i < rep->event_count && count < max_out; i++) {
        if (rep->events[i].tick == rep->current_tick) {
            out[count++] = rep->events[i];
        }
    }

    rep->current_tick++;
    if (rep->current_tick > rep->to_tick) rep->active = false;

    return count;
}

bool replay_done(const replay_t *rep) {
    if (rep->event_count == 0) return true;
    return !rep->active;
}
