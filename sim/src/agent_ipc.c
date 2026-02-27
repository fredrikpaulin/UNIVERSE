/*
 * agent_ipc.c — Agent IPC protocol, observation/action serialization,
 *               fallback agent, and agent routing.
 *
 * Hand-rolled JSON: no library dependency. We write minimal JSON serialization
 * and a simple recursive-descent-free parser (just strstr + sscanf for the
 * small set of fields we care about).
 */
#include "agent_ipc.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include <stdarg.h>

/* ---- Name tables ---- */

static const char *RESOURCE_NAMES[RES_COUNT] = {
    "iron", "silicon", "rare_earth", "water",
    "hydrogen", "helium3", "carbon", "uranium", "exotic"
};

static const char *ACTION_NAMES[ACT_COUNT] = {
    "navigate_to_body", "enter_orbit", "land", "launch",
    "survey", "mine", "wait", "repair", "travel_to_system", "replicate",
    "send_message", "place_beacon", "build_structure", "trade"
};

static const char *LOCATION_NAMES[] = {
    "interstellar", "in_system", "orbiting", "landed", "docked"
};

static const char *STATUS_NAMES[] = {
    "active", "traveling", "mining", "building",
    "replicating", "dormant", "damaged", "destroyed"
};

/* ---- Name ↔ enum lookups ---- */

resource_t resource_from_name(const char *name) {
    for (int i = 0; i < RES_COUNT; i++) {
        if (strcmp(name, RESOURCE_NAMES[i]) == 0) return (resource_t)i;
    }
    return (resource_t)-1;
}

const char *resource_to_name(resource_t r) {
    if ((int)r >= 0 && (int)r < RES_COUNT) return RESOURCE_NAMES[(int)r];
    return "unknown";
}

action_type_t action_type_from_name(const char *name) {
    for (int i = 0; i < ACT_COUNT; i++) {
        if (strcmp(name, ACTION_NAMES[i]) == 0) return (action_type_t)i;
    }
    return (action_type_t)-1;
}

const char *action_type_to_name(action_type_t t) {
    if ((int)t >= 0 && (int)t < ACT_COUNT) return ACTION_NAMES[(int)t];
    return "unknown";
}

/* ---- JSON writing helpers ---- */

/* Append to buffer, tracking position. Returns chars written. */
static int js_append(char *buf, int pos, int max, const char *fmt, ...) {
    if (pos >= max) return 0;
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf + pos, (size_t)(max - pos), fmt, ap);
    va_end(ap);
    if (n < 0) return 0;
    return n;
}

/* ---- Observation serialization ---- */

int obs_serialize(const probe_t *probe, const system_t *sys,
                  char *buf, int buf_size) {
    int p = 0;

    /* Root object */
    p += js_append(buf, p, buf_size, "{");

    /* Tick */
    p += js_append(buf, p, buf_size, "\"tick\":%llu,",
                   (unsigned long long)probe->created_tick);

    /* Probe section */
    p += js_append(buf, p, buf_size, "\"probe\":{");
    p += js_append(buf, p, buf_size, "\"name\":\"%s\",", probe->name);
    p += js_append(buf, p, buf_size, "\"generation\":%u,", probe->generation);
    p += js_append(buf, p, buf_size, "\"status\":\"%s\",",
                   STATUS_NAMES[probe->status]);
    p += js_append(buf, p, buf_size, "\"location_type\":\"%s\",",
                   LOCATION_NAMES[probe->location_type]);
    p += js_append(buf, p, buf_size, "\"fuel_kg\":%.1f,", probe->fuel_kg);
    p += js_append(buf, p, buf_size, "\"energy_joules\":%.3e,", probe->energy_joules);
    p += js_append(buf, p, buf_size, "\"hull_integrity\":%.3f,", (double)probe->hull_integrity);
    p += js_append(buf, p, buf_size, "\"mass_kg\":%.1f,", probe->mass_kg);
    p += js_append(buf, p, buf_size, "\"max_speed_c\":%.4f,", (double)probe->max_speed_c);
    p += js_append(buf, p, buf_size, "\"sensor_range_ly\":%.1f,",
                   (double)probe->sensor_range_ly);
    p += js_append(buf, p, buf_size, "\"speed_c\":%.6f,", probe->speed_c);
    p += js_append(buf, p, buf_size, "\"travel_remaining_ly\":%.3f",
                   probe->travel_remaining_ly);
    p += js_append(buf, p, buf_size, "},");

    /* System section */
    p += js_append(buf, p, buf_size, "\"system\":{");
    p += js_append(buf, p, buf_size, "\"name\":\"%s\",", sys->name);
    p += js_append(buf, p, buf_size, "\"star_count\":%u,", sys->star_count);
    p += js_append(buf, p, buf_size, "\"planet_count\":%u,", sys->planet_count);
    p += js_append(buf, p, buf_size, "\"visited\":%s", sys->visited ? "true" : "false");

    /* Stars */
    p += js_append(buf, p, buf_size, ",\"stars\":[");
    for (int i = 0; i < sys->star_count; i++) {
        if (i > 0) p += js_append(buf, p, buf_size, ",");
        p += js_append(buf, p, buf_size,
            "{\"name\":\"%s\",\"class\":%d,\"mass_solar\":%.3f,\"temp_k\":%.0f}",
            sys->stars[i].name, (int)sys->stars[i].class,
            sys->stars[i].mass_solar, sys->stars[i].temperature_k);
    }
    p += js_append(buf, p, buf_size, "]");

    /* Planets */
    p += js_append(buf, p, buf_size, ",\"planets\":[");
    for (int i = 0; i < sys->planet_count; i++) {
        if (i > 0) p += js_append(buf, p, buf_size, ",");
        const planet_t *pl = &sys->planets[i];
        p += js_append(buf, p, buf_size,
            "{\"name\":\"%s\",\"type\":%d,\"mass_earth\":%.3f,"
            "\"orbital_radius_au\":%.3f,\"habitability\":%.3f}",
            pl->name, (int)pl->type, pl->mass_earth,
            pl->orbital_radius_au, pl->habitability_index);
    }
    p += js_append(buf, p, buf_size, "]");

    p += js_append(buf, p, buf_size, "}"); /* close system */
    p += js_append(buf, p, buf_size, "}"); /* close root */

    if (p >= buf_size) return -1; /* overflow */
    return p;
}

/* ---- Minimal JSON field extraction ---- */

/* Find "key":value in a JSON string and extract the string value.
 * Returns pointer to start of value, or NULL. */
static const char *json_find_str(const char *json, const char *key, char *out, int out_max) {
    /* Build search pattern: "key":" */
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":\"", key);
    const char *pos = strstr(json, pattern);
    if (!pos) return NULL;
    pos += strlen(pattern);
    int i = 0;
    while (*pos && *pos != '"' && i < out_max - 1) {
        out[i++] = *pos++;
    }
    out[i] = '\0';
    return out;
}

/* Find "key":number and extract integer value. Returns 0 on success. */
static int json_find_int(const char *json, const char *key, long long *out) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *pos = strstr(json, pattern);
    if (!pos) return -1;
    pos += strlen(pattern);
    /* Skip whitespace */
    while (*pos == ' ' || *pos == '\t') pos++;
    char *end;
    *out = strtoll(pos, &end, 10);
    if (end == pos) return -1;
    return 0;
}

/* ---- Action parsing ---- */

int action_parse(const char *json, action_t *out) {
    if (!json || !out || strlen(json) < 2) return -1;

    memset(out, 0, sizeof(*out));

    /* Find "action":"..." */
    char action_name[64];
    if (!json_find_str(json, "action", action_name, sizeof(action_name))) {
        return -1;
    }

    action_type_t type = action_type_from_name(action_name);
    if ((int)type == -1) return -1;
    out->type = type;

    /* Parse optional fields based on action type */
    if (type == ACT_MINE) {
        char res_name[64];
        if (json_find_str(json, "resource", res_name, sizeof(res_name))) {
            out->target_resource = resource_from_name(res_name);
        }
    }

    if (type == ACT_SURVEY) {
        long long level = 0;
        if (json_find_int(json, "level", &level) == 0) {
            out->survey_level = (int)level;
        }
    }

    if (type == ACT_NAVIGATE_TO_BODY) {
        long long hi = 0, lo = 0;
        json_find_int(json, "target_body_hi", &hi);
        json_find_int(json, "target_body_lo", &lo);
        out->target_body = (probe_uid_t){(uint64_t)hi, (uint64_t)lo};
    }

    if (type == ACT_TRAVEL_TO_SYSTEM) {
        /* Parse target_system_id string "hi-lo" */
        char sys_id_str[64] = {0};
        if (json_find_str(json, "target_system_id", sys_id_str, sizeof(sys_id_str))) {
            uint64_t hi = 0, lo = 0;
            hi = strtoull(sys_id_str, NULL, 10);
            const char *dash = strchr(sys_id_str, '-');
            if (dash) lo = strtoull(dash + 1, NULL, 10);
            out->target_system = (probe_uid_t){hi, lo};
        }
        /* Parse optional sector [x,y,z] */
        long long sx = 0, sy = 0, sz = 0;
        json_find_int(json, "sector_x", &sx);
        json_find_int(json, "sector_y", &sy);
        json_find_int(json, "sector_z", &sz);
        out->target_sector = (sector_coord_t){(int)sx, (int)sy, (int)sz};
    }

    if (type == ACT_SEND_MESSAGE) {
        /* Parse target probe "hi-lo" and message content */
        char tgt_str[64] = {0};
        if (json_find_str(json, "target", tgt_str, sizeof(tgt_str))) {
            uint64_t hi = 0, lo = 0;
            hi = strtoull(tgt_str, NULL, 10);
            const char *dash = strchr(tgt_str, '-');
            if (dash) lo = strtoull(dash + 1, NULL, 10);
            out->target_probe = (probe_uid_t){hi, lo};
        }
        json_find_str(json, "content", out->message, sizeof(out->message));
    }

    if (type == ACT_PLACE_BEACON) {
        json_find_str(json, "message", out->message, sizeof(out->message));
    }

    if (type == ACT_BUILD_STRUCTURE) {
        long long stype = 0;
        if (json_find_int(json, "structure_type", &stype) == 0) {
            out->structure_type = (int)stype;
        }
    }

    if (type == ACT_TRADE) {
        char tgt_str[64] = {0};
        if (json_find_str(json, "target", tgt_str, sizeof(tgt_str))) {
            uint64_t hi = 0, lo = 0;
            hi = strtoull(tgt_str, NULL, 10);
            const char *dash = strchr(tgt_str, '-');
            if (dash) lo = strtoull(dash + 1, NULL, 10);
            out->target_probe = (probe_uid_t){hi, lo};
        }
        char res_name[64];
        if (json_find_str(json, "resource", res_name, sizeof(res_name))) {
            out->target_resource = resource_from_name(res_name);
        }
        long long amt = 0;
        if (json_find_int(json, "amount", &amt) == 0) {
            out->amount = (double)amt;
        }
    }

    return 0;
}

/* ---- Action result serialization ---- */

int result_serialize(const action_result_t *res, char *buf, int buf_size) {
    int p = 0;
    p += js_append(buf, p, buf_size, "{");
    p += js_append(buf, p, buf_size, "\"success\":%s,", res->success ? "true" : "false");
    p += js_append(buf, p, buf_size, "\"completed\":%s", res->completed ? "true" : "false");
    if (!res->success && res->error[0]) {
        p += js_append(buf, p, buf_size, ",\"error\":\"%s\"", res->error);
    }
    p += js_append(buf, p, buf_size, "}");
    if (p >= buf_size) return -1;
    return p;
}

/* ---- Fallback agent ---- */

action_t fallback_agent_decide(const probe_t *probe) {
    action_t act;
    memset(&act, 0, sizeof(act));

    /* If traveling, can only wait */
    if (probe->status == STATUS_TRAVELING) {
        act.type = ACT_WAIT;
        return act;
    }

    /* If damaged, try to repair */
    if (probe->hull_integrity < 0.99f) {
        act.type = ACT_REPAIR;
        return act;
    }

    /* Otherwise just wait */
    act.type = ACT_WAIT;
    return act;
}

/* ---- Protocol framing ---- */

int protocol_frame(const char *msg, char *out, int out_size) {
    int len = (int)strlen(msg);
    if (len + 1 >= out_size) return -1;
    memcpy(out, msg, (size_t)len);
    out[len] = '\n';
    return len + 1;
}

int protocol_unframe(const char *buf, int buf_len, char *out, int out_size) {
    /* Find first newline */
    int nl_pos = -1;
    for (int i = 0; i < buf_len; i++) {
        if (buf[i] == '\n') { nl_pos = i; break; }
    }
    if (nl_pos < 0) return -1;
    if (nl_pos >= out_size) return -1;
    memcpy(out, buf, (size_t)nl_pos);
    out[nl_pos] = '\0';
    return nl_pos;
}

/* ---- Agent router ---- */

void agent_router_init(agent_router_t *r) {
    r->count = 0;
    for (int i = 0; i < MAX_AGENTS; i++) {
        r->slots[i].fd = -1;
        r->slots[i].probe_id = uid_null();
    }
}

void agent_router_destroy(agent_router_t *r) {
    /* Don't close fds — caller owns them. Just clear state. */
    r->count = 0;
    for (int i = 0; i < MAX_AGENTS; i++) {
        r->slots[i].fd = -1;
    }
}

int agent_router_register(agent_router_t *r, probe_uid_t probe_id, int fd) {
    /* Check if already registered */
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (r->slots[i].fd >= 0 && uid_eq(r->slots[i].probe_id, probe_id)) {
            r->slots[i].fd = fd; /* update fd */
            return 0;
        }
    }
    /* Find empty slot */
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (r->slots[i].fd < 0) {
            r->slots[i].probe_id = probe_id;
            r->slots[i].fd = fd;
            r->count++;
            return 0;
        }
    }
    return -1; /* full */
}

void agent_router_unregister(agent_router_t *r, probe_uid_t probe_id) {
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (r->slots[i].fd >= 0 && uid_eq(r->slots[i].probe_id, probe_id)) {
            r->slots[i].fd = -1;
            r->slots[i].probe_id = uid_null();
            r->count--;
            return;
        }
    }
}

int agent_router_lookup(const agent_router_t *r, probe_uid_t probe_id) {
    for (int i = 0; i < MAX_AGENTS; i++) {
        if (r->slots[i].fd >= 0 && uid_eq(r->slots[i].probe_id, probe_id)) {
            return r->slots[i].fd;
        }
    }
    return -1;
}
