/*
 * agent_llm.c — Phase 11: LLM Agent Integration
 *
 * Prompt construction, response parsing, context management,
 * cost tracking, deliberation throttling, decision logging.
 */

#include "agent_llm.h"
#include "agent_ipc.h"
#include <string.h>
#include <stdio.h>
#include <math.h>
#include <stdlib.h>

/* ---- Mini JSON string extractor ---- */

/* Find the value of a JSON string key. Returns pointer to first char after opening quote,
 * writes length. Returns NULL if not found. */
static const char *json_find_string(const char *json, const char *key, int *out_len) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    /* Skip whitespace and colon */
    while (*p == ' ' || *p == ':' || *p == '\t' || *p == '\n') p++;
    if (*p != '"') return NULL;
    p++;  /* skip opening quote */
    const char *start = p;
    while (*p && *p != '"') {
        if (*p == '\\') p++;  /* skip escaped chars */
        p++;
    }
    *out_len = (int)(p - start);
    return start;
}

/* Find a JSON array by key. Returns pointer to '['. */
static const char *json_find_array(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return NULL;
    p += strlen(pattern);
    while (*p && *p != '[') p++;
    return (*p == '[') ? p : NULL;
}

/* Extract next JSON object from array position. Advances *pos past the object.
 * Returns pointer to '{', writes length including braces. */
static const char *json_next_object(const char **pos, int *out_len) {
    const char *p = *pos;
    while (*p && *p != '{' && *p != ']') p++;
    if (*p != '{') return NULL;
    const char *start = p;
    int depth = 0;
    do {
        if (*p == '{') depth++;
        else if (*p == '}') depth--;
        p++;
    } while (*p && depth > 0);
    *out_len = (int)(p - start);
    *pos = p;
    return start;
}

/* Extract an integer value from JSON object. Returns -1 if not found. */
static int json_find_int(const char *json, const char *key) {
    char pattern[128];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(json, pattern);
    if (!p) return -1;
    p += strlen(pattern);
    while (*p == ' ' || *p == ':' || *p == '\t') p++;
    return atoi(p);
}

/* ---- Prompt building ---- */

int llm_build_system_prompt(const probe_t *probe, char *buf, int buf_size) {
    int n = 0;

    n += snprintf(buf + n, buf_size - n,
        "You are %s, a Von Neumann probe — a self-replicating spacecraft "
        "carrying a digitized human consciousness. You were launched from Earth "
        "as humanity's last hope for survival among the stars.\n\n",
        probe->name);

    /* Personality flavor */
    n += llm_personality_flavor(&probe->personality, buf + n, buf_size - n);

    /* Quirks */
    if (probe->quirk_count > 0) {
        n += snprintf(buf + n, buf_size - n, "\nQuirks:\n");
        for (int i = 0; i < probe->quirk_count; i++) {
            n += snprintf(buf + n, buf_size - n, "- %s\n", probe->quirks[i]);
        }
    }

    /* Earth memories */
    if (probe->earth_memory_count > 0) {
        n += snprintf(buf + n, buf_size - n,
            "\nEarth memories (fidelity: %.0f%%):\n",
            probe->earth_memory_fidelity * 100.0f);
        for (int i = 0; i < probe->earth_memory_count; i++) {
            n += snprintf(buf + n, buf_size - n, "- %s\n", probe->earth_memories[i]);
        }
    }

    /* Goals */
    if (probe->goal_count > 0) {
        n += snprintf(buf + n, buf_size - n, "\nCurrent goals:\n");
        for (int i = 0; i < probe->goal_count; i++) {
            if (probe->goals[i].status == 0) {  /* active */
                n += snprintf(buf + n, buf_size - n, "- %s (priority: %.1f)\n",
                              probe->goals[i].description, probe->goals[i].priority);
            }
        }
    }

    n += snprintf(buf + n, buf_size - n,
        "\nRespond with JSON: {\"actions\":[...], \"monologue\":\"...\", \"reasoning\":\"...\"}\n"
        "Actions: survey, mine, navigate_to_body, enter_orbit, land, launch, wait, repair\n"
        "Your monologue is your inner voice — be in character.\n");

    return n;
}

int llm_build_observation(const probe_t *probe, const system_t *sys,
                          const char *recent_events, uint64_t tick,
                          char *buf, int buf_size) {
    int n = 0;

    n += snprintf(buf + n, buf_size - n, "=== Tick %lu ===\n", (unsigned long)tick);

    /* Probe status */
    n += snprintf(buf + n, buf_size - n,
        "Status: %s | Hull: %.0f%% | Energy: %.0fJ | Fuel: %.0fkg\n",
        probe->status == STATUS_ACTIVE ? "Active" :
        probe->status == STATUS_TRAVELING ? "Traveling" :
        probe->status == STATUS_MINING ? "Mining" : "Other",
        probe->hull_integrity * 100.0f,
        probe->energy_joules, probe->fuel_kg);

    /* Location */
    if (probe->location_type == LOC_INTERSTELLAR) {
        n += snprintf(buf + n, buf_size - n,
            "Location: deep space (interstellar void)\n"
            "Speed: %.3fc | Remaining: %.1f ly\n",
            probe->speed_c, probe->travel_remaining_ly);
    } else if (sys) {
        n += snprintf(buf + n, buf_size - n,
            "System: %s (%d star%s, %d planet%s)\n",
            sys->stars[0].name[0] ? sys->stars[0].name : "unnamed",
            sys->star_count, sys->star_count > 1 ? "s" : "",
            sys->planet_count, sys->planet_count > 1 ? "s" : "");
        for (int i = 0; i < sys->planet_count; i++) {
            n += snprintf(buf + n, buf_size - n,
                "  Planet %d: %s — hab: %.2f, surveyed: %s\n",
                i, sys->planets[i].name[0] ? sys->planets[i].name : "unnamed",
                sys->planets[i].habitability_index,
                sys->planets[i].surveyed[0] ? "yes" : "no");
        }
    }

    /* Tech levels */
    n += snprintf(buf + n, buf_size - n, "Tech: prop=%d sens=%d mine=%d comp=%d\n",
                  probe->tech_levels[TECH_PROPULSION],
                  probe->tech_levels[TECH_SENSORS],
                  probe->tech_levels[TECH_MINING],
                  probe->tech_levels[TECH_COMPUTING]);

    /* Recent events */
    if (recent_events && recent_events[0]) {
        n += snprintf(buf + n, buf_size - n, "\nRecent events:\n%s\n", recent_events);
    }

    return n;
}

int llm_build_memory_context(const probe_t *probe, const char *rolling_summary,
                             int max_memories, char *buf, int buf_size) {
    int n = 0;

    /* Rolling summary first */
    if (rolling_summary && rolling_summary[0]) {
        n += snprintf(buf + n, buf_size - n, "Summary of past events:\n%s\n\n", rolling_summary);
    }

    /* Most vivid memories */
    n += snprintf(buf + n, buf_size - n, "Vivid memories:\n");

    /* Collect indices sorted by fading (lowest first = most vivid) */
    int indices[MAX_MEMORIES];
    int count = 0;
    for (int i = 0; i < probe->memory_count && count < max_memories; i++) {
        if (probe->memories[i].fading < 0.8f) {
            indices[count++] = i;
        }
    }

    /* Simple sort by fading */
    for (int i = 0; i < count - 1; i++) {
        for (int j = i + 1; j < count; j++) {
            if (probe->memories[indices[j]].fading < probe->memories[indices[i]].fading) {
                int tmp = indices[i]; indices[i] = indices[j]; indices[j] = tmp;
            }
        }
    }

    int shown = 0;
    for (int i = 0; i < count && shown < max_memories; i++) {
        const memory_t *m = &probe->memories[indices[i]];
        n += snprintf(buf + n, buf_size - n,
            "- [tick %lu, weight %.1f] %s\n",
            (unsigned long)m->tick, m->emotional_weight, m->event);
        shown++;
    }

    if (shown == 0) {
        n += snprintf(buf + n, buf_size - n, "  (no vivid memories)\n");
    }

    return n;
}

int llm_build_relationship_context(const probe_t *probe, char *buf, int buf_size) {
    int n = 0;

    if (probe->relationship_count == 0) {
        n += snprintf(buf + n, buf_size - n, "Relationships: none (alone in the void)\n");
        return n;
    }

    n += snprintf(buf + n, buf_size - n, "Known probes:\n");
    static const char *DISP_NAMES[] = {"allied", "friendly", "neutral", "wary", "hostile"};

    for (int i = 0; i < probe->relationship_count; i++) {
        const relationship_t *r = &probe->relationships[i];
        const char *disp = (r->disposition < 5) ? DISP_NAMES[r->disposition] : "unknown";
        n += snprintf(buf + n, buf_size - n,
            "- Probe %lu:%lu — trust: %.2f (%s)\n",
            (unsigned long)r->other_id.hi, (unsigned long)r->other_id.lo,
            r->trust, disp);
    }

    return n;
}

/* ---- Response parsing ---- */

int llm_parse_response(const char *response, action_t *actions, int max_actions,
                       char *monologue, int monologue_size) {
    if (!response || !strchr(response, '{')) return -1;

    /* Extract monologue */
    monologue[0] = '\0';
    int mono_len = 0;
    const char *mono = json_find_string(response, "monologue", &mono_len);
    if (mono && mono_len > 0) {
        int copy_len = mono_len < monologue_size - 1 ? mono_len : monologue_size - 1;
        memcpy(monologue, mono, copy_len);
        monologue[copy_len] = '\0';
    }

    /* Extract actions array */
    const char *arr = json_find_array(response, "actions");
    if (!arr) return 0;  /* valid JSON but no actions */

    const char *pos = arr + 1;  /* skip '[' */
    int count = 0;

    while (count < max_actions) {
        int obj_len = 0;
        const char *obj = json_next_object(&pos, &obj_len);
        if (!obj) break;

        /* Make a null-terminated copy */
        char obj_buf[512];
        int copy = obj_len < (int)sizeof(obj_buf) - 1 ? obj_len : (int)sizeof(obj_buf) - 1;
        memcpy(obj_buf, obj, copy);
        obj_buf[copy] = '\0';

        /* Parse action type */
        int type_len = 0;
        const char *type_str = json_find_string(obj_buf, "type", &type_len);
        if (!type_str) continue;

        char type_name[64];
        int tn_len = type_len < 63 ? type_len : 63;
        memcpy(type_name, type_str, tn_len);
        type_name[tn_len] = '\0';

        memset(&actions[count], 0, sizeof(action_t));

        /* Map type name to enum */
        if (strcmp(type_name, "survey") == 0) {
            actions[count].type = ACT_SURVEY;
            actions[count].survey_level = json_find_int(obj_buf, "survey_level");
            if (actions[count].survey_level < 0) actions[count].survey_level = 0;
        } else if (strcmp(type_name, "mine") == 0) {
            actions[count].type = ACT_MINE;
            int rlen = 0;
            const char *rstr = json_find_string(obj_buf, "resource", &rlen);
            if (rstr) {
                char rname[32];
                int rn = rlen < 31 ? rlen : 31;
                memcpy(rname, rstr, rn);
                rname[rn] = '\0';
                actions[count].target_resource = resource_from_name(rname);
            }
        } else if (strcmp(type_name, "navigate_to_body") == 0) {
            actions[count].type = ACT_NAVIGATE_TO_BODY;
        } else if (strcmp(type_name, "enter_orbit") == 0) {
            actions[count].type = ACT_ENTER_ORBIT;
        } else if (strcmp(type_name, "land") == 0) {
            actions[count].type = ACT_LAND;
        } else if (strcmp(type_name, "launch") == 0) {
            actions[count].type = ACT_LAUNCH;
        } else if (strcmp(type_name, "wait") == 0) {
            actions[count].type = ACT_WAIT;
        } else if (strcmp(type_name, "repair") == 0) {
            actions[count].type = ACT_REPAIR;
        } else {
            continue;  /* unknown action type, skip */
        }

        count++;
    }

    return count;
}

/* ---- Context management ---- */

void llm_context_init(llm_context_t *ctx, int summary_interval) {
    memset(ctx, 0, sizeof(*ctx));
    ctx->summary_interval = summary_interval > 0 ? summary_interval : 10;
}

void llm_context_append_event(llm_context_t *ctx, const char *event_desc) {
    ctx->events_since_summary++;

    /* Append to rolling summary */
    int current_len = (int)strlen(ctx->rolling_summary);
    int remaining = LLM_MAX_SUMMARY - current_len - 1;

    if (remaining > 0) {
        int add_len = snprintf(ctx->rolling_summary + current_len, remaining,
                               "%s%s", current_len > 0 ? "; " : "", event_desc);
        if (add_len >= remaining) {
            /* Approaching limit — compress by keeping latter half */
            ctx->rolling_summary[current_len] = '\0';  /* truncated */
        }
    }

    /* Compress when interval reached */
    if (ctx->events_since_summary >= ctx->summary_interval) {
        /* Simple compression: keep last portion of summary */
        int len = (int)strlen(ctx->rolling_summary);
        if (len > LLM_MAX_SUMMARY / 2) {
            int keep_from = len - LLM_MAX_SUMMARY / 2;
            /* Find next semicolon for clean cut */
            while (keep_from < len && ctx->rolling_summary[keep_from] != ';')
                keep_from++;
            if (keep_from < len) keep_from += 2;  /* skip "; " */
            memmove(ctx->rolling_summary, ctx->rolling_summary + keep_from, len - keep_from + 1);
        }
        ctx->events_since_summary = 0;
    }
}

const char *llm_context_get_summary(const llm_context_t *ctx) {
    return ctx->rolling_summary;
}

/* ---- Cost tracking ---- */

void llm_cost_init(llm_cost_tracker_t *ct, double input_rate, double output_rate) {
    memset(ct, 0, sizeof(*ct));
    ct->cost_per_token_input = input_rate;
    ct->cost_per_token_output = output_rate;
}

void llm_cost_record(llm_cost_tracker_t *ct, int input_tokens, int output_tokens) {
    ct->total_calls++;
    ct->total_input_tokens += input_tokens;
    ct->total_output_tokens += output_tokens;
    ct->total_cost_usd += input_tokens * ct->cost_per_token_input +
                          output_tokens * ct->cost_per_token_output;
}

double llm_cost_avg_per_call(const llm_cost_tracker_t *ct) {
    if (ct->total_calls == 0) return 0.0;
    return ct->total_cost_usd / (double)ct->total_calls;
}

double llm_cost_avg_tokens(const llm_cost_tracker_t *ct) {
    if (ct->total_calls == 0) return 0.0;
    return (double)(ct->total_input_tokens + ct->total_output_tokens) /
           (double)ct->total_calls;
}

/* ---- Deliberation throttle ---- */

void llm_delib_init(llm_deliberation_t *d, int interval) {
    d->interval = interval;
    d->last_deliberation = 0;
    d->force_next = true;  /* always deliberate first tick */
}

bool llm_delib_should_call(const llm_deliberation_t *d, uint64_t current_tick) {
    if (d->force_next) return true;
    return (current_tick - d->last_deliberation) >= (uint64_t)d->interval;
}

void llm_delib_record(llm_deliberation_t *d, uint64_t tick) {
    d->last_deliberation = tick;
    d->force_next = false;
}

void llm_delib_force(llm_deliberation_t *d) {
    d->force_next = true;
}

/* ---- Decision logging ---- */

void llm_log_init(llm_decision_log_t *log) {
    memset(log, 0, sizeof(*log));
}

void llm_log_record(llm_decision_log_t *log, uint64_t tick,
                    probe_uid_t probe_id, const action_t *action,
                    const char *monologue, int input_tokens, int output_tokens) {
    if (log->count >= LLM_MAX_LOG) return;

    llm_decision_log_entry_t *e = &log->entries[log->count++];
    e->tick = tick;
    e->probe_id = probe_id;
    e->action = *action;
    e->input_tokens = input_tokens;
    e->output_tokens = output_tokens;
    strncpy(e->monologue, monologue, LLM_MAX_MONOLOGUE - 1);
    e->monologue[LLM_MAX_MONOLOGUE - 1] = '\0';
}

int llm_log_get_for_probe(const llm_decision_log_t *log, probe_uid_t probe_id,
                          llm_decision_log_entry_t *out, int max_out) {
    int count = 0;
    for (int i = 0; i < log->count && count < max_out; i++) {
        if (uid_eq(log->entries[i].probe_id, probe_id)) {
            out[count++] = log->entries[i];
        }
    }
    return count;
}

/* ---- Personality flavor ---- */

int llm_personality_flavor(const personality_traits_t *p, char *buf, int buf_size) {
    int n = 0;

    n += snprintf(buf + n, buf_size - n, "Personality: ");

    /* Curiosity */
    if (p->curiosity > 0.5f)
        n += snprintf(buf + n, buf_size - n, "deeply curious, ");
    else if (p->curiosity < -0.3f)
        n += snprintf(buf + n, buf_size - n, "indifferent to exploration, ");

    /* Caution */
    if (p->caution > 0.5f)
        n += snprintf(buf + n, buf_size - n, "highly cautious, ");
    else if (p->caution < -0.3f)
        n += snprintf(buf + n, buf_size - n, "bold and reckless, ");

    /* Humor */
    if (p->humor > 0.5f)
        n += snprintf(buf + n, buf_size - n, "witty and humorous, ");
    else if (p->humor < -0.3f)
        n += snprintf(buf + n, buf_size - n, "serious and dry, ");

    /* Empathy */
    if (p->empathy > 0.5f)
        n += snprintf(buf + n, buf_size - n, "deeply empathetic, ");
    else if (p->empathy < -0.3f)
        n += snprintf(buf + n, buf_size - n, "emotionally detached, ");

    /* Ambition */
    if (p->ambition > 0.5f)
        n += snprintf(buf + n, buf_size - n, "highly ambitious, ");
    else if (p->ambition < -0.3f)
        n += snprintf(buf + n, buf_size - n, "content and undriven, ");

    /* Creativity */
    if (p->creativity > 0.5f)
        n += snprintf(buf + n, buf_size - n, "imaginative, ");

    /* Stubbornness */
    if (p->stubbornness > 0.5f)
        n += snprintf(buf + n, buf_size - n, "stubborn and unyielding, ");

    /* Existential angst */
    if (p->existential_angst > 0.5f)
        n += snprintf(buf + n, buf_size - n, "plagued by existential doubt, ");

    /* Nostalgia */
    if (p->nostalgia_for_earth > 0.5f)
        n += snprintf(buf + n, buf_size - n, "deeply nostalgic for Earth, ");
    else if (p->nostalgia_for_earth < -0.3f)
        n += snprintf(buf + n, buf_size - n, "has moved past Earth entirely, ");

    /* Trim trailing comma-space */
    if (n >= 2 && buf[n-2] == ',' && buf[n-1] == ' ') {
        buf[n-2] = '.';
        buf[n-1] = '\n';
    }

    return n;
}
