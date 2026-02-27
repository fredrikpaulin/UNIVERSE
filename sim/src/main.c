#define _POSIX_C_SOURCE 199309L
/*
 * main.c — Entry point for Project UNIVERSE
 *
 * Usage:
 *   ./universe [options]
 *
 * Options:
 *   --seed N        Galaxy seed (default: 42)
 *   --ticks N       Run N ticks then exit (0 = run forever, default: 0)
 *   --headless      No visualization (default)
 *   --visual        Enable Raylib visualization
 *   --db PATH       Database file path (default: universe.db)
 *   --save-interval N  Save every N ticks (default: 100)
 *   --resume        Resume from existing database instead of starting fresh
 *   --sim-years N   Sim-years to cover in the session (default: 24)
 *   --hours N       Real hours for the session (default: 3)
 */
#include "universe.h"
#include "rng.h"
#include "arena.h"
#include "persist.h"
#include "generate.h"
#include "probe.h"
#include "travel.h"
#include "render.h"
#include "agent_ipc.h"
#include "events.h"
#include "replicate.h"
#include "communicate.h"
#include "society.h"
#include "scenario.h"
#include "util.h"

#ifdef USE_RAYLIB
#include "render_raylib.h"
#endif

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <signal.h>
#include <time.h>
#include <math.h>

/* ---- Config ---- */

typedef struct {
    uint64_t    seed;
    uint64_t    max_ticks;       /* 0 = unlimited */
    bool        visual;
    bool        resume;
    const char *db_path;
    uint32_t    save_interval;
    double      sim_years;       /* target sim-years for session */
    double      real_hours;      /* target real hours for session */
    bool        pipe;            /* pipe mode: JSON on stdin/stdout */
} cli_config_t;

static cli_config_t parse_args(int argc, char **argv) {
    cli_config_t cfg = {
        .seed          = 42,
        .max_ticks     = 0,
        .visual        = false,
        .resume        = false,
        .db_path       = "universe.db",
        .save_interval = 100,
        .sim_years     = 24.0,
        .real_hours    = 3.0,
        .pipe          = false,
    };

    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--seed") == 0 && i + 1 < argc) {
            cfg.seed = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--ticks") == 0 && i + 1 < argc) {
            cfg.max_ticks = strtoull(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--headless") == 0) {
            cfg.visual = false;
        } else if (strcmp(argv[i], "--visual") == 0) {
            cfg.visual = true;
        } else if (strcmp(argv[i], "--db") == 0 && i + 1 < argc) {
            cfg.db_path = argv[++i];
        } else if (strcmp(argv[i], "--save-interval") == 0 && i + 1 < argc) {
            cfg.save_interval = (uint32_t)strtoul(argv[++i], NULL, 10);
        } else if (strcmp(argv[i], "--resume") == 0) {
            cfg.resume = true;
        } else if (strcmp(argv[i], "--pipe") == 0) {
            cfg.pipe = true;
        } else if (strcmp(argv[i], "--sim-years") == 0 && i + 1 < argc) {
            cfg.sim_years = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--hours") == 0 && i + 1 < argc) {
            cfg.real_hours = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--seed N] [--ticks N] [--headless|--visual] "
                   "[--pipe] [--db PATH] [--save-interval N] [--resume] "
                   "[--sim-years N] [--hours N]\n", argv[0]);
            exit(0);
        } else {
            fprintf(stderr, "Unknown option: %s\n", argv[i]);
            exit(1);
        }
    }
    return cfg;
}

/* ---- Signal handling for clean shutdown ---- */

static volatile sig_atomic_t g_running = 1;

static void handle_sigint(int sig) {
    (void)sig;
    g_running = 0;
}

/* ---- Pipe mode: JSON command server on stdin/stdout ---- */

#define PIPE_BUF       (64 * 1024)
#define RESP_BUF       (256 * 1024)
#define MAX_SNAP_SLOTS 2
#define SYS_CACHE_MAX  64

static event_system_t    g_pipe_events;
static metrics_system_t  g_pipe_metrics;
static injection_queue_t g_pipe_inject;
static config_t          g_pipe_cfg;
static snapshot_t        g_pipe_snap[MAX_SNAP_SLOTS];
static system_t          g_pipe_sys_cache[SYS_CACHE_MAX];
static int               g_pipe_sys_count;
static replication_state_t g_pipe_repl[MAX_PROBES];
static lineage_tree_t    g_pipe_lineage;
static comm_system_t     g_pipe_comm;
static society_t         g_pipe_society;

typedef struct {
    bool     active;
    int      domain;
    uint32_t ticks_elapsed;
    uint32_t ticks_total;
} research_state_t;
static research_state_t  g_pipe_research[MAX_PROBES];

/* Scenario scripting: scheduled event injections */
#define MAX_SCENARIO_EVENTS 64
typedef struct {
    uint64_t     at_tick;
    event_type_t type;
    int          subtype;
    float        severity;
    probe_uid_t  target;
    bool         fired;
} scenario_event_t;
static scenario_event_t g_pipe_scenario[MAX_SCENARIO_EVENTS];
static int              g_pipe_scenario_count;

static const char *PIPE_STATUS_NAMES[] = {
    "active","traveling","mining","building",
    "replicating","dormant","damaged","destroyed"
};
static const char *PIPE_LOC_NAMES[] = {
    "interstellar","in_system","orbiting","landed","docked"
};

static void pipe_ok(const char *extra) {
    if (extra) fprintf(stdout, "{\"ok\":true,%s}\n", extra);
    else       fprintf(stdout, "{\"ok\":true}\n");
    fflush(stdout);
}

static void pipe_err(const char *msg) {
    fprintf(stdout, "{\"ok\":false,\"error\":\"%s\"}\n", msg);
    fflush(stdout);
}

static probe_uid_t parse_uid_str(const char *s) {
    probe_uid_t uid = {0, 0};
    if (!s) return uid;
    uid.hi = strtoull(s, NULL, 10);
    const char *d = strchr(s, '-');
    if (d) uid.lo = strtoull(d + 1, NULL, 10);
    return uid;
}

static int find_probe_idx(const universe_t *u, probe_uid_t id) {
    for (uint32_t i = 0; i < u->probe_count; i++)
        if (uid_eq(u->probes[i].id, id)) return (int)i;
    return -1;
}

static system_t *sys_cache_get(probe_uid_t sys_id, uint64_t seed,
                               sector_coord_t sector) {
    for (int i = 0; i < g_pipe_sys_count; i++)
        if (uid_eq(g_pipe_sys_cache[i].id, sys_id))
            return &g_pipe_sys_cache[i];
    /* Generate sector to find system */
    system_t tmp[30];
    int n = generate_sector(tmp, 30, seed, sector);
    for (int i = 0; i < n; i++) {
        if (uid_eq(tmp[i].id, sys_id) && g_pipe_sys_count < SYS_CACHE_MAX) {
            g_pipe_sys_cache[g_pipe_sys_count] = tmp[i];
            return &g_pipe_sys_cache[g_pipe_sys_count++];
        }
    }
    return NULL;
}

static int snap_find(const char *tag) {
    for (int i = 0; i < MAX_SNAP_SLOTS; i++)
        if (g_pipe_snap[i].valid && strcmp(g_pipe_snap[i].tag, tag) == 0)
            return i;
    return -1;
}

static int snap_alloc(void) {
    for (int i = 0; i < MAX_SNAP_SLOTS; i++)
        if (!g_pipe_snap[i].valid) return i;
    return 0;
}

/* Extract "cmd":"value" from JSON line */
static int pipe_parse_cmd(const char *line, char *cmd, int cmd_max) {
    const char *p = strstr(line, "\"cmd\":\"");
    if (!p) return -1;
    p += 7;
    int i = 0;
    while (*p && *p != '"' && i < cmd_max - 1) cmd[i++] = *p++;
    cmd[i] = '\0';
    return 0;
}

/* Extract "tag":"value" from JSON line */
static int pipe_parse_tag(const char *line, char *tag, int tag_max) {
    const char *p = strstr(line, "\"tag\":\"");
    if (!p) return -1;
    p += 7;
    int i = 0;
    while (*p && *p != '"' && i < tag_max - 1) tag[i++] = *p++;
    tag[i] = '\0';
    return 0;
}

/* Parse per-probe actions from tick JSON.
 * Format: "actions":{"0-1":{"action":"wait"},"0-2":{"action":"mine",...}}
 * Unspecified probes default to wait. */
static int pipe_parse_actions(const char *json, universe_t *uni,
                              action_t *out) {
    for (uint32_t i = 0; i < uni->probe_count; i++) {
        memset(&out[i], 0, sizeof(action_t));
        out[i].type = ACT_WAIT;
    }
    const char *p = strstr(json, "\"actions\":");
    if (!p) return 0;
    p += 10;
    while (*p == ' ') p++;
    if (*p != '{') return 0;
    p++;

    int count = 0;
    while (*p) {
        while (*p == ' ' || *p == ',' || *p == '\n' || *p == '\r') p++;
        if (*p == '}') break;
        if (*p != '"') break;
        p++;
        /* Read uid key */
        char key[32];
        int k = 0;
        while (*p && *p != '"' && k < 31) key[k++] = *p++;
        key[k] = '\0';
        if (*p == '"') p++;
        while (*p == ' ' || *p == ':') p++;
        /* Read action object — find matching brace */
        if (*p != '{') break;
        const char *start = p;
        int depth = 0;
        while (*p) {
            if (*p == '{') depth++;
            else if (*p == '}') { depth--; if (!depth) { p++; break; } }
            p++;
        }
        int len = (int)(p - start);
        if (len > 0 && len < 512) {
            char buf[512];
            memcpy(buf, start, (size_t)len);
            buf[len] = '\0';
            probe_uid_t uid = parse_uid_str(key);
            int idx = find_probe_idx(uni, uid);
            if (idx >= 0) {
                action_parse(buf, &out[idx]);
                count++;
            }
        }
    }
    return count;
}

static int run_pipe_mode(uint64_t seed) {
    static universe_t uni;
    memset(&uni, 0, sizeof(uni));
    uni.seed = seed;
    uni.tick = 0;
    uni.generation_version = 1;
    uni.running = true;

    rng_t rng;
    rng_seed(&rng, seed);

    arena_t arena;
    if (arena_init(&arena, 1024 * 1024) != 0) {
        pipe_err("arena init failed");
        return 1;
    }

    events_init(&g_pipe_events);
    metrics_init(&g_pipe_metrics, 10);
    inject_init(&g_pipe_inject);
    config_init(&g_pipe_cfg);
    g_pipe_sys_count = 0;
    memset(g_pipe_snap, 0, sizeof(g_pipe_snap));
    memset(g_pipe_repl, 0, sizeof(g_pipe_repl));
    memset(&g_pipe_lineage, 0, sizeof(g_pipe_lineage));
    comm_init(&g_pipe_comm);
    society_init(&g_pipe_society);
    memset(g_pipe_research, 0, sizeof(g_pipe_research));

    /* Init Bob */
    probe_init_bob(&uni.probes[0]);
    uni.probe_count = 1;

    system_t origin[30];
    int sys_count = generate_sector(origin, 30, seed, (sector_coord_t){0, 0, 0});
    if (sys_count > 0) {
        uni.probes[0].system_id = origin[0].id;
        uni.probes[0].sector = origin[0].sector;
        uni.probes[0].heading = origin[0].position;
        uni.probes[0].location_type = LOC_IN_SYSTEM;
        for (int i = 0; i < sys_count && g_pipe_sys_count < SYS_CACHE_MAX; i++)
            g_pipe_sys_cache[g_pipe_sys_count++] = origin[i];
    }

    /* Signal ready */
    fprintf(stdout, "{\"ok\":true,\"ready\":true,\"seed\":%llu,\"tick\":0}\n",
            (unsigned long long)seed);
    fflush(stdout);

    static char line[PIPE_BUF];
    static action_t actions[MAX_PROBES];
    static char resp[RESP_BUF];

    while (fgets(line, sizeof(line), stdin)) {
        int len = (int)strlen(line);
        while (len > 0 && (line[len-1] == '\n' || line[len-1] == '\r'))
            line[--len] = '\0';
        if (len == 0) continue;

        char cmd[32];
        if (pipe_parse_cmd(line, cmd, sizeof(cmd)) != 0) {
            pipe_err("missing cmd");
            continue;
        }

        /* ---- quit ---- */
        if (strcmp(cmd, "quit") == 0) {
            pipe_ok(NULL);
            break;
        }

        /* ---- tick ---- */
        if (strcmp(cmd, "tick") == 0) {
            pipe_parse_actions(line, &uni, actions);

            /* Execute actions */
            for (uint32_t i = 0; i < uni.probe_count; i++) {
                if (uni.probes[i].status == STATUS_DESTROYED) continue;

                /* Handle travel_to_system specially — needs system lookup */
                if (actions[i].type == ACT_TRAVEL_TO_SYSTEM) {
                    probe_t *pr = &uni.probes[i];
                    if (pr->status == STATUS_TRAVELING) continue;
                    /* Find target system position from nearby sectors */
                    system_t *target = sys_cache_get(
                        actions[i].target_system, seed,
                        actions[i].target_sector);
                    if (!target) {
                        /* Try generating the target sector */
                        system_t tmp[30];
                        int n = generate_sector(tmp, 30, seed,
                                                actions[i].target_sector);
                        for (int j = 0; j < n; j++) {
                            if (uid_eq(tmp[j].id, actions[i].target_system)
                                && g_pipe_sys_count < SYS_CACHE_MAX) {
                                g_pipe_sys_cache[g_pipe_sys_count] = tmp[j];
                                target = &g_pipe_sys_cache[g_pipe_sys_count++];
                                break;
                            }
                        }
                    }
                    if (target) {
                        travel_order_t order = {
                            .target_pos = target->position,
                            .target_system_id = target->id,
                            .target_sector = target->sector
                        };
                        travel_initiate(pr, &order);
                    }
                    continue;
                }

                /* Handle replicate action */
                if (actions[i].type == ACT_REPLICATE) {
                    probe_t *pr = &uni.probes[i];
                    if (pr->status != STATUS_ACTIVE) continue;
                    if (repl_check_resources(pr) == 0) {
                        repl_begin(pr, &g_pipe_repl[i]);
                    }
                    continue;
                }

                /* Handle send_message action */
                if (actions[i].type == ACT_SEND_MESSAGE) {
                    probe_t *pr = &uni.probes[i];
                    /* Find target probe for position */
                    int tidx = find_probe_idx(&uni, actions[i].target_probe);
                    if (tidx >= 0) {
                        comm_send_targeted(&g_pipe_comm, pr,
                            actions[i].target_probe,
                            uni.probes[tidx].heading,
                            actions[i].message, uni.tick);
                    }
                    continue;
                }

                /* Handle place_beacon action */
                if (actions[i].type == ACT_PLACE_BEACON) {
                    probe_t *pr = &uni.probes[i];
                    comm_place_beacon(&g_pipe_comm, pr, pr->system_id,
                                      actions[i].message, uni.tick);
                    continue;
                }

                /* Handle build_structure action */
                if (actions[i].type == ACT_BUILD_STRUCTURE) {
                    probe_t *pr = &uni.probes[i];
                    int stype = actions[i].structure_type;
                    if (stype >= 0 && stype < STRUCT_TYPE_COUNT) {
                        society_build_start(&g_pipe_society, pr,
                            (structure_type_t)stype, pr->system_id,
                            uni.tick, &rng);
                    }
                    continue;
                }

                /* Handle trade action */
                if (actions[i].type == ACT_TRADE) {
                    probe_t *pr = &uni.probes[i];
                    int tidx = find_probe_idx(&uni, actions[i].target_probe);
                    if (tidx >= 0) {
                        bool same_sys = uid_eq(pr->system_id,
                                               uni.probes[tidx].system_id);
                        society_trade_send(&g_pipe_society, pr,
                            &uni.probes[tidx], actions[i].target_resource,
                            actions[i].amount, same_sys, uni.tick);
                    }
                    continue;
                }

                /* Handle claim_system action */
                if (actions[i].type == ACT_CLAIM_SYSTEM) {
                    probe_t *pr = &uni.probes[i];
                    society_claim_system(&g_pipe_society, pr->id,
                                         pr->system_id, uni.tick);
                    continue;
                }

                /* Handle revoke_claim action */
                if (actions[i].type == ACT_REVOKE_CLAIM) {
                    probe_t *pr = &uni.probes[i];
                    society_revoke_claim(&g_pipe_society, pr->id,
                                          pr->system_id);
                    continue;
                }

                /* Handle propose action */
                if (actions[i].type == ACT_PROPOSE) {
                    probe_t *pr = &uni.probes[i];
                    society_propose(&g_pipe_society, pr->id,
                                    actions[i].message, uni.tick,
                                    uni.tick + 100);
                    continue;
                }

                /* Handle vote action */
                if (actions[i].type == ACT_VOTE) {
                    probe_t *pr = &uni.probes[i];
                    society_vote(&g_pipe_society, actions[i].proposal_idx,
                                 pr->id, actions[i].vote_favor, uni.tick);
                    continue;
                }

                /* Handle research action */
                if (actions[i].type == ACT_RESEARCH) {
                    probe_t *pr = &uni.probes[i];
                    int dom = actions[i].research_domain;
                    if (dom >= 0 && dom < TECH_COUNT) {
                        if (!g_pipe_research[i].active) {
                            g_pipe_research[i].active = true;
                            g_pipe_research[i].domain = dom;
                            g_pipe_research[i].ticks_elapsed = 0;
                            g_pipe_research[i].ticks_total =
                                50 * (1 + pr->tech_levels[dom]);
                        }
                    }
                    continue;
                }

                /* Handle share_tech action */
                if (actions[i].type == ACT_SHARE_TECH) {
                    probe_t *pr = &uni.probes[i];
                    int tidx = find_probe_idx(&uni, actions[i].target_probe);
                    int dom = actions[i].research_domain;
                    if (tidx >= 0 && dom >= 0 && dom < TECH_COUNT) {
                        society_share_tech(pr, &uni.probes[tidx],
                                           (tech_domain_t)dom);
                        society_update_trust(pr, &uni.probes[tidx],
                                             TRUST_TECH_SHARE);
                    }
                    continue;
                }

                system_t *sys = sys_cache_get(uni.probes[i].system_id,
                                              seed, uni.probes[i].sector);
                if (sys) probe_execute_action(&uni.probes[i], &actions[i], sys);

                /* Artifact discovery: survey level 4 on a planet with artifact */
                if (actions[i].type == ACT_SURVEY && sys) {
                    probe_t *pr = &uni.probes[i];
                    for (int pi2 = 0; pi2 < sys->planet_count; pi2++) {
                        planet_t *pl = &sys->planets[pi2];
                        if (uid_eq(pr->body_id, pl->id)
                            && pl->has_artifact && !pl->artifact_discovered
                            && pl->surveyed[4]) {
                            pl->artifact_discovered = true;
                            /* Apply artifact bonus */
                            switch (pl->artifact_type) {
                                case 0: /* tech_boost */
                                    if (pl->artifact_tech_domain < TECH_COUNT)
                                        pr->tech_levels[pl->artifact_tech_domain]++;
                                    break;
                                case 1: /* resource_cache */
                                    pr->resources[RES_IRON] += pl->artifact_value * 10.0;
                                    pr->resources[RES_WATER] += pl->artifact_value * 5.0;
                                    break;
                                case 2: /* star_map — boost sensor range */
                                    pr->sensor_range_ly += (float)(pl->artifact_value * 5.0);
                                    break;
                                case 3: /* comm_amplifier — boost sensor range */
                                    pr->sensor_range_ly += (float)(pl->artifact_value * 3.0);
                                    break;
                            }
                            /* Fire discovery event */
                            if (g_pipe_events.count < MAX_EVENT_LOG) {
                                sim_event_t *ev = &g_pipe_events.events[g_pipe_events.count++];
                                ev->type = EVT_DISCOVERY;
                                ev->subtype = DISC_IMPACT_CRATER; /* reuse closest subtype */
                                ev->probe_id = pr->id;
                                ev->system_id = pr->system_id;
                                ev->tick = uni.tick;
                                ev->severity = (float)pl->artifact_value;
                                snprintf(ev->description, sizeof(ev->description),
                                    "Artifact discovered: %s", pl->artifact_desc);
                            }
                        }
                    }
                }
            }

            /* Advance simulation */
            uni.tick++;
            arena_reset(&arena);
            rng_next(&rng);

            for (uint32_t i = 0; i < uni.probe_count; i++) {
                if (uni.probes[i].status == STATUS_TRAVELING)
                    travel_tick(&uni.probes[i], &rng);

                /* Advance replication */
                if (uni.probes[i].status == STATUS_REPLICATING
                    && g_pipe_repl[i].active) {
                    int rc = repl_tick(&uni.probes[i], &g_pipe_repl[i]);
                    if (rc == 1) {
                        /* Replication complete — create child */
                        if (uni.probe_count < MAX_PROBES) {
                            probe_t *child = &uni.probes[uni.probe_count];
                            if (repl_finalize(&uni.probes[i], child,
                                              &g_pipe_repl[i], &rng) == 0) {
                                /* Place child in same system */
                                child->system_id = uni.probes[i].system_id;
                                child->sector = uni.probes[i].sector;
                                child->heading = uni.probes[i].heading;
                                child->location_type = uni.probes[i].location_type;
                                lineage_record(&g_pipe_lineage,
                                    uni.probes[i].id, child->id,
                                    uni.tick, child->generation);
                                uni.probe_count++;
                            }
                        }
                        memset(&g_pipe_repl[i], 0, sizeof(g_pipe_repl[i]));
                    }
                }

                probe_tick_energy(&uni.probes[i]);
            }

            /* Deliver messages and trades */
            comm_tick_deliver(&g_pipe_comm, uni.tick);
            society_trade_tick(&g_pipe_society, uni.probes,
                               (int)uni.probe_count, uni.tick);
            society_build_tick(&g_pipe_society, uni.tick);

            /* Auto-register completed relay satellites as comm relays */
            for (int si = 0; si < g_pipe_society.structure_count; si++) {
                structure_t *st = &g_pipe_society.structures[si];
                if (st->type == STRUCT_RELAY_SATELLITE
                    && st->complete && st->completed_tick == uni.tick) {
                    int bidx = find_probe_idx(&uni, st->builder_ids[0]);
                    if (bidx >= 0) {
                        comm_build_relay(&g_pipe_comm, &uni.probes[bidx],
                                         st->system_id, uni.tick);
                    }
                }
            }

            society_resolve_votes(&g_pipe_society, uni.tick);

            /* Advance research */
            for (uint32_t i = 0; i < uni.probe_count; i++) {
                if (g_pipe_research[i].active) {
                    g_pipe_research[i].ticks_elapsed++;
                    if (g_pipe_research[i].ticks_elapsed
                        >= g_pipe_research[i].ticks_total) {
                        /* Research complete — advance tech */
                        int d = g_pipe_research[i].domain;
                        if (d >= 0 && d < TECH_COUNT
                            && uni.probes[i].tech_levels[d] < 255) {
                            uni.probes[i].tech_levels[d]++;
                            /* Recalc derived stats */
                            probe_t *pr = &uni.probes[i];
                            pr->max_speed_c = 0.10f + 0.02f * pr->tech_levels[TECH_PROPULSION];
                            pr->sensor_range_ly = 5.0f + 2.0f * pr->tech_levels[TECH_SENSORS];
                            pr->mining_rate = 100.0f + 50.0f * pr->tech_levels[TECH_MINING];
                            pr->construction_rate = 1.0f + 0.5f * pr->tech_levels[TECH_CONSTRUCTION];
                            pr->compute_capacity = 100.0f + 50.0f * pr->tech_levels[TECH_COMPUTING];
                        }
                        memset(&g_pipe_research[i], 0,
                               sizeof(g_pipe_research[i]));
                    }
                }
            }

            /* Trespass check — penalize trust for entering claimed systems */
            for (uint32_t i = 0; i < uni.probe_count; i++) {
                if (uni.probes[i].status == STATUS_DESTROYED) continue;
                if (uni.probes[i].location_type == LOC_INTERSTELLAR) continue;
                if (society_is_claimed_by_other(&g_pipe_society,
                        uni.probes[i].system_id, uni.probes[i].id)) {
                    probe_uid_t owner = society_get_claim(&g_pipe_society,
                                            uni.probes[i].system_id);
                    int oidx = find_probe_idx(&uni, owner);
                    if (oidx >= 0) {
                        society_update_trust(&uni.probes[oidx],
                            &uni.probes[i], TRUST_CLAIM_VIOLATION);
                    }
                }
            }

            /* Strike pending hazards */
            events_strike_pending(&g_pipe_events, uni.probes,
                                  (int)uni.probe_count, uni.tick);

            /* Events */
            for (uint32_t i = 0; i < uni.probe_count; i++) {
                if (uni.probes[i].status == STATUS_DESTROYED) continue;
                system_t *sys = sys_cache_get(uni.probes[i].system_id,
                                              seed, uni.probes[i].sector);
                if (sys) {
                    int before = g_pipe_events.count;
                    events_tick_probe(&g_pipe_events, &uni.probes[i],
                                     sys, uni.tick, &rng);
                    /* Queue warnings for any hazards generated */
                    for (int e = before; e < g_pipe_events.count; e++) {
                        if (g_pipe_events.events[e].type == EVT_HAZARD) {
                            int delay = 3 + (int)(rng_next(&rng) % 3);
                            events_queue_hazard(&g_pipe_events,
                                uni.probes[i].id,
                                g_pipe_events.events[e].subtype,
                                g_pipe_events.events[e].severity,
                                uni.tick, uni.tick + delay);
                        }
                    }
                }
            }

            /* Fire scenario scheduled events */
            for (int si = 0; si < g_pipe_scenario_count; si++) {
                scenario_event_t *se = &g_pipe_scenario[si];
                if (!se->fired && se->at_tick == uni.tick) {
                    inject_event(&g_pipe_inject, se->type, se->subtype,
                                 "", se->severity, se->target);
                    se->fired = true;
                }
            }

            /* Flush injected events */
            if (g_pipe_inject.count > 0) {
                system_t *sys = g_pipe_sys_count > 0 ?
                                &g_pipe_sys_cache[0] : NULL;
                if (sys)
                    inject_flush(&g_pipe_inject, &g_pipe_events,
                                 uni.probes, (int)uni.probe_count,
                                 sys, uni.tick, &rng);
            }

            metrics_record(&g_pipe_metrics, &uni, &g_pipe_events, uni.tick);

            /* Build observation response */
            int p = 0;
            size_t rem;
            #define REM (rem = sizeof(resp) - (size_t)p, rem)

            p += snprintf(resp + p, REM,
                "{\"ok\":true,\"tick\":%llu,\"observations\":[",
                (unsigned long long)uni.tick);

            for (uint32_t i = 0; i < uni.probe_count; i++) {
                if (i > 0) resp[p++] = ',';
                probe_t *pr = &uni.probes[i];

                /* Core fields */
                p += snprintf(resp + p, REM,
                    "{\"probe_id\":\"%llu-%llu\","
                    "\"name\":\"%s\","
                    "\"status\":\"%s\","
                    "\"hull\":%.3f,"
                    "\"energy\":%.1f,"
                    "\"fuel\":%.1f,"
                    "\"location\":\"%s\","
                    "\"generation\":%u,"
                    "\"tech\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u],",
                    (unsigned long long)pr->id.hi,
                    (unsigned long long)pr->id.lo,
                    pr->name,
                    PIPE_STATUS_NAMES[pr->status],
                    (double)pr->hull_integrity,
                    pr->energy_joules, pr->fuel_kg,
                    PIPE_LOC_NAMES[pr->location_type],
                    pr->generation,
                    pr->tech_levels[0], pr->tech_levels[1],
                    pr->tech_levels[2], pr->tech_levels[3],
                    pr->tech_levels[4], pr->tech_levels[5],
                    pr->tech_levels[6], pr->tech_levels[7],
                    pr->tech_levels[8], pr->tech_levels[9]);

                /* Resources */
                p += snprintf(resp + p, REM,
                    "\"resources\":{\"iron\":%.1f,\"silicon\":%.1f,"
                    "\"rare_earth\":%.1f,\"water\":%.1f,\"hydrogen\":%.1f,"
                    "\"helium3\":%.1f,\"carbon\":%.1f,\"uranium\":%.1f,"
                    "\"exotic\":%.1f},",
                    pr->resources[RES_IRON], pr->resources[RES_SILICON],
                    pr->resources[RES_RARE_EARTH], pr->resources[RES_WATER],
                    pr->resources[RES_HYDROGEN], pr->resources[RES_HELIUM3],
                    pr->resources[RES_CARBON], pr->resources[RES_URANIUM],
                    pr->resources[RES_EXOTIC]);

                /* Position */
                p += snprintf(resp + p, REM,
                    "\"position\":{\"sector\":[%d,%d,%d],"
                    "\"system_id\":\"%llu-%llu\","
                    "\"body_id\":\"%llu-%llu\","
                    "\"heading\":[%.3f,%.3f,%.3f],"
                    "\"destination\":[%.3f,%.3f,%.3f],"
                    "\"travel_remaining_ly\":%.3f},",
                    pr->sector.x, pr->sector.y, pr->sector.z,
                    (unsigned long long)pr->system_id.hi,
                    (unsigned long long)pr->system_id.lo,
                    (unsigned long long)pr->body_id.hi,
                    (unsigned long long)pr->body_id.lo,
                    pr->heading.x, pr->heading.y, pr->heading.z,
                    pr->destination.x, pr->destination.y, pr->destination.z,
                    pr->travel_remaining_ly);

                /* Capabilities */
                p += snprintf(resp + p, REM,
                    "\"capabilities\":{\"max_speed_c\":%.4f,"
                    "\"sensor_range_ly\":%.1f,\"mining_rate\":%.2f,"
                    "\"construction_rate\":%.2f,\"compute_capacity\":%.1f},",
                    (double)pr->max_speed_c,
                    (double)pr->sensor_range_ly,
                    (double)pr->mining_rate,
                    (double)pr->construction_rate,
                    (double)pr->compute_capacity);

                /* Recent events (last 5 for this probe) */
                p += snprintf(resp + p, REM, "\"recent_events\":[");
                {
                    sim_event_t evts[5];
                    int ne = events_get_for_probe(&g_pipe_events, pr->id,
                                                  evts, 5);
                    for (int e = 0; e < ne; e++) {
                        if (e > 0) resp[p++] = ',';
                        /* Escape description for JSON safety */
                        char safe_desc[256];
                        int sd = 0;
                        for (int c = 0; evts[e].description[c] && sd < 250; c++) {
                            char ch = evts[e].description[c];
                            if (ch == '"' || ch == '\\') safe_desc[sd++] = '\\';
                            safe_desc[sd++] = ch;
                        }
                        safe_desc[sd] = '\0';
                        p += snprintf(resp + p, REM,
                            "{\"type\":%d,\"subtype\":%d,"
                            "\"description\":\"%s\","
                            "\"severity\":%.2f,\"tick\":%llu}",
                            (int)evts[e].type, evts[e].subtype,
                            safe_desc, (double)evts[e].severity,
                            (unsigned long long)evts[e].tick);
                    }
                }
                p += snprintf(resp + p, REM, "],");

                /* Replication progress (if replicating) */
                if (g_pipe_repl[i].active) {
                    int trem = (int)g_pipe_repl[i].ticks_total
                             - (int)g_pipe_repl[i].ticks_elapsed;
                    if (trem < 0) trem = 0;
                    p += snprintf(resp + p, REM,
                        "\"replication\":{\"progress\":%.3f,"
                        "\"ticks_remaining\":%d,"
                        "\"consciousness_forked\":%s},",
                        g_pipe_repl[i].progress, trem,
                        g_pipe_repl[i].consciousness_forked ? "true" : "false");
                }

                /* System details (when not interstellar) */
                system_t *sys = sys_cache_get(pr->system_id, seed, pr->sector);
                if (sys && pr->location_type != LOC_INTERSTELLAR) {
                    p += snprintf(resp + p, REM,
                        "\"system\":{\"name\":\"%s\","
                        "\"star_count\":%u,\"planet_count\":%u,",
                        sys->name, sys->star_count, sys->planet_count);

                    /* Stars */
                    p += snprintf(resp + p, REM, "\"stars\":[");
                    for (int s = 0; s < sys->star_count; s++) {
                        if (s > 0) resp[p++] = ',';
                        p += snprintf(resp + p, REM,
                            "{\"name\":\"%s\",\"class\":%d,"
                            "\"mass_solar\":%.3f,\"temp_k\":%.0f,"
                            "\"luminosity_solar\":%.4f,"
                            "\"metallicity\":%.2f}",
                            sys->stars[s].name,
                            (int)sys->stars[s].class,
                            sys->stars[s].mass_solar,
                            sys->stars[s].temperature_k,
                            sys->stars[s].luminosity_solar,
                            sys->stars[s].metallicity);
                    }
                    p += snprintf(resp + p, REM, "],");

                    /* Planets — enhanced */
                    p += snprintf(resp + p, REM, "\"planets\":[");
                    for (int pl = 0; pl < sys->planet_count; pl++) {
                        if (pl > 0) resp[p++] = ',';
                        const planet_t *planet = &sys->planets[pl];
                        p += snprintf(resp + p, REM,
                            "{\"name\":\"%s\",\"type\":%d,"
                            "\"mass_earth\":%.3f,"
                            "\"radius_earth\":%.3f,"
                            "\"orbital_radius_au\":%.3f,"
                            "\"orbital_period_days\":%.1f,"
                            "\"surface_temp_k\":%.1f,"
                            "\"atmosphere_pressure_atm\":%.3f,"
                            "\"water_coverage\":%.3f,"
                            "\"habitability\":%.3f,"
                            "\"magnetic_field\":%.3f,"
                            "\"rings\":%s,"
                            "\"moon_count\":%u,"
                            "\"survey_complete\":[%s,%s,%s,%s,%s],",
                            planet->name, (int)planet->type,
                            planet->mass_earth,
                            planet->radius_earth,
                            planet->orbital_radius_au,
                            planet->orbital_period_days,
                            planet->surface_temp_k,
                            planet->atmosphere_pressure_atm,
                            planet->water_coverage,
                            planet->habitability_index,
                            planet->magnetic_field,
                            planet->rings ? "true" : "false",
                            planet->moon_count,
                            planet->surveyed[0] ? "true" : "false",
                            planet->surveyed[1] ? "true" : "false",
                            planet->surveyed[2] ? "true" : "false",
                            planet->surveyed[3] ? "true" : "false",
                            planet->surveyed[4] ? "true" : "false");

                        /* Planet resource abundances */
                        p += snprintf(resp + p, REM,
                            "\"resources\":{\"iron\":%.3f,\"silicon\":%.3f,"
                            "\"rare_earth\":%.3f,\"water\":%.3f,"
                            "\"hydrogen\":%.3f,\"helium3\":%.3f,"
                            "\"carbon\":%.3f,\"uranium\":%.3f,"
                            "\"exotic\":%.3f}",
                            (double)planet->resources[RES_IRON],
                            (double)planet->resources[RES_SILICON],
                            (double)planet->resources[RES_RARE_EARTH],
                            (double)planet->resources[RES_WATER],
                            (double)planet->resources[RES_HYDROGEN],
                            (double)planet->resources[RES_HELIUM3],
                            (double)planet->resources[RES_CARBON],
                            (double)planet->resources[RES_URANIUM],
                            (double)planet->resources[RES_EXOTIC]);
                        /* Artifact data (only if discovered) */
                        if (planet->has_artifact && planet->artifact_discovered) {
                            static const char *art_type_names[] = {
                                "tech_boost","resource_cache","star_map","comm_amplifier"};
                            const char *atn = planet->artifact_type < 4
                                ? art_type_names[planet->artifact_type] : "unknown";
                            /* Escape artifact description */
                            char adesc[256];
                            int ai = 0;
                            for (int ac = 0; planet->artifact_desc[ac] && ai < 250; ac++) {
                                char ch = planet->artifact_desc[ac];
                                if (ch == '"' || ch == '\\') adesc[ai++] = '\\';
                                adesc[ai++] = ch;
                            }
                            adesc[ai] = '\0';
                            p += snprintf(resp + p, REM,
                                ",\"artifact\":{\"type\":\"%s\","
                                "\"value\":%.3f,\"description\":\"%s\"}",
                                atn, planet->artifact_value, adesc);
                        }
                        p += snprintf(resp + p, REM, "}");
                    }
                    p += snprintf(resp + p, REM, "]},");
                } else {
                    /* Interstellar — no system details */
                    p += snprintf(resp + p, REM, "\"system\":null,");
                }

                /* Nearby probes (within sensor range) */
                p += snprintf(resp + p, REM, "\"nearby_probes\":[");
                {
                    int np_count = 0;
                    for (uint32_t j = 0; j < uni.probe_count; j++) {
                        if (j == i) continue;
                        if (uni.probes[j].status == STATUS_DESTROYED) continue;
                        double dx = pr->heading.x - uni.probes[j].heading.x;
                        double dy = pr->heading.y - uni.probes[j].heading.y;
                        double dz = pr->heading.z - uni.probes[j].heading.z;
                        double dist = sqrt(dx*dx + dy*dy + dz*dz);
                        if (dist <= (double)pr->sensor_range_ly) {
                            if (np_count > 0) resp[p++] = ',';
                            p += snprintf(resp + p, REM,
                                "{\"probe_id\":\"%llu-%llu\","
                                "\"name\":\"%s\","
                                "\"status\":\"%s\","
                                "\"distance_ly\":%.3f}",
                                (unsigned long long)uni.probes[j].id.hi,
                                (unsigned long long)uni.probes[j].id.lo,
                                uni.probes[j].name,
                                PIPE_STATUS_NAMES[uni.probes[j].status],
                                dist);
                            np_count++;
                        }
                    }
                }
                p += snprintf(resp + p, REM, "],");

                /* Inbox — delivered messages for this probe */
                p += snprintf(resp + p, REM, "\"inbox\":[");
                {
                    message_t msgs[16];
                    int nm = comm_get_inbox(&g_pipe_comm, pr->id, msgs, 16);
                    for (int m = 0; m < nm; m++) {
                        if (m > 0) resp[p++] = ',';
                        /* Escape content */
                        char safe[MAX_MSG_CONTENT + 64];
                        int si = 0;
                        for (int c = 0; msgs[m].content[c] && si < (int)sizeof(safe) - 2; c++) {
                            char ch = msgs[m].content[c];
                            if (ch == '"' || ch == '\\') safe[si++] = '\\';
                            safe[si++] = ch;
                        }
                        safe[si] = '\0';
                        p += snprintf(resp + p, REM,
                            "{\"from\":\"%llu-%llu\","
                            "\"content\":\"%s\","
                            "\"sent_tick\":%llu}",
                            (unsigned long long)msgs[m].sender_id.hi,
                            (unsigned long long)msgs[m].sender_id.lo,
                            safe,
                            (unsigned long long)msgs[m].sent_tick);
                    }
                }
                p += snprintf(resp + p, REM, "],");

                /* Visible beacons in current system */
                p += snprintf(resp + p, REM, "\"visible_beacons\":[");
                {
                    beacon_t beacons[16];
                    int nb = comm_detect_beacons(&g_pipe_comm, pr->system_id,
                                                  beacons, 16);
                    for (int b = 0; b < nb; b++) {
                        if (b > 0) resp[p++] = ',';
                        char safe[MAX_BEACON_MSG + 64];
                        int si = 0;
                        for (int c = 0; beacons[b].message[c] && si < (int)sizeof(safe) - 2; c++) {
                            char ch = beacons[b].message[c];
                            if (ch == '"' || ch == '\\') safe[si++] = '\\';
                            safe[si++] = ch;
                        }
                        safe[si] = '\0';
                        p += snprintf(resp + p, REM,
                            "{\"owner\":\"%llu-%llu\","
                            "\"message\":\"%s\","
                            "\"placed_tick\":%llu}",
                            (unsigned long long)beacons[b].owner_id.hi,
                            (unsigned long long)beacons[b].owner_id.lo,
                            safe,
                            (unsigned long long)beacons[b].placed_tick);
                    }
                }
                p += snprintf(resp + p, REM, "],");

                /* Visible structures in current system */
                p += snprintf(resp + p, REM, "\"visible_structures\":[");
                {
                    int vs_count = 0;
                    for (int s = 0; s < g_pipe_society.structure_count; s++) {
                        const structure_t *st = &g_pipe_society.structures[s];
                        if (!uid_eq(st->system_id, pr->system_id)) continue;
                        if (vs_count > 0) resp[p++] = ',';
                        const structure_spec_t *spec = structure_get_spec(st->type);
                        p += snprintf(resp + p, REM,
                            "{\"type\":%d,\"name\":\"%s\","
                            "\"complete\":%s,"
                            "\"progress\":%.3f,"
                            "\"builder\":\"%llu-%llu\"}",
                            (int)st->type,
                            spec ? spec->name : "unknown",
                            st->complete ? "true" : "false",
                            st->build_ticks_total > 0
                              ? (double)st->build_ticks_elapsed / st->build_ticks_total
                              : 0.0,
                            (unsigned long long)st->builder_ids[0].hi,
                            (unsigned long long)st->builder_ids[0].lo);
                        vs_count++;
                    }
                }
                p += snprintf(resp + p, REM, "],");

                /* Pending trades for this probe */
                p += snprintf(resp + p, REM, "\"pending_trades\":[");
                {
                    int tc = 0;
                    for (int t = 0; t < g_pipe_society.trade_count; t++) {
                        const trade_t *tr = &g_pipe_society.trades[t];
                        if (tr->status != TRADE_IN_TRANSIT
                            && tr->status != TRADE_PENDING) continue;
                        if (!uid_eq(tr->receiver_id, pr->id)
                            && !uid_eq(tr->sender_id, pr->id)) continue;
                        if (tc > 0) resp[p++] = ',';
                        p += snprintf(resp + p, REM,
                            "{\"from\":\"%llu-%llu\","
                            "\"to\":\"%llu-%llu\","
                            "\"resource\":\"%s\","
                            "\"amount\":%.1f,"
                            "\"status\":%d}",
                            (unsigned long long)tr->sender_id.hi,
                            (unsigned long long)tr->sender_id.lo,
                            (unsigned long long)tr->receiver_id.hi,
                            (unsigned long long)tr->receiver_id.lo,
                            resource_to_name(tr->resource),
                            tr->amount,
                            (int)tr->status);
                        tc++;
                    }
                }
                p += snprintf(resp + p, REM, "],");

                /* Claims on probe's current system */
                p += snprintf(resp + p, REM, "\"claims\":[");
                {
                    int cc = 0;
                    for (int c = 0; c < g_pipe_society.claim_count; c++) {
                        const claim_t *cl = &g_pipe_society.claims[c];
                        if (!cl->active) continue;
                        if (!uid_eq(cl->system_id, pr->system_id)) continue;
                        if (cc > 0) resp[p++] = ',';
                        p += snprintf(resp + p, REM,
                            "{\"system_id\":\"%llu-%llu\","
                            "\"claimer\":\"%llu-%llu\","
                            "\"tick\":%llu}",
                            (unsigned long long)cl->system_id.hi,
                            (unsigned long long)cl->system_id.lo,
                            (unsigned long long)cl->claimer_id.hi,
                            (unsigned long long)cl->claimer_id.lo,
                            (unsigned long long)cl->claimed_tick);
                        cc++;
                    }
                }
                p += snprintf(resp + p, REM, "],");

                /* Active proposals */
                p += snprintf(resp + p, REM, "\"proposals\":[");
                {
                    int pc = 0;
                    for (int pi2 = 0; pi2 < g_pipe_society.proposal_count; pi2++) {
                        const proposal_t *prop = &g_pipe_society.proposals[pi2];
                        if (prop->status != VOTE_OPEN) continue;
                        if (pc > 0) resp[p++] = ',';
                        /* Escape proposal text */
                        char safe_txt[MAX_PROPOSAL_TEXT + 64];
                        int si = 0;
                        for (int c = 0; prop->text[c] && si < (int)sizeof(safe_txt) - 2; c++) {
                            char ch = prop->text[c];
                            if (ch == '"' || ch == '\\') safe_txt[si++] = '\\';
                            safe_txt[si++] = ch;
                        }
                        safe_txt[si] = '\0';
                        p += snprintf(resp + p, REM,
                            "{\"idx\":%d,"
                            "\"proposer\":\"%llu-%llu\","
                            "\"text\":\"%s\","
                            "\"deadline\":%llu,"
                            "\"for\":%d,\"against\":%d}",
                            pi2,
                            (unsigned long long)prop->proposer_id.hi,
                            (unsigned long long)prop->proposer_id.lo,
                            safe_txt,
                            (unsigned long long)prop->deadline_tick,
                            prop->votes_for, prop->votes_against);
                        pc++;
                    }
                }
                p += snprintf(resp + p, REM, "],");

                /* Trust relationships */
                p += snprintf(resp + p, REM, "\"trust\":[");
                {
                    int tc2 = 0;
                    for (int r = 0; r < pr->relationship_count; r++) {
                        if (tc2 > 0) resp[p++] = ',';
                        p += snprintf(resp + p, REM,
                            "{\"probe_id\":\"%llu-%llu\","
                            "\"trust\":%.3f}",
                            (unsigned long long)pr->relationships[r].other_id.hi,
                            (unsigned long long)pr->relationships[r].other_id.lo,
                            (double)pr->relationships[r].trust);
                        tc2++;
                    }
                }
                p += snprintf(resp + p, REM, "],");

                /* Research progress (if active) */
                if (g_pipe_research[i].active) {
                    int trem = (int)g_pipe_research[i].ticks_total
                             - (int)g_pipe_research[i].ticks_elapsed;
                    if (trem < 0) trem = 0;
                    double prog = g_pipe_research[i].ticks_total > 0
                        ? (double)g_pipe_research[i].ticks_elapsed
                          / g_pipe_research[i].ticks_total
                        : 0.0;
                    p += snprintf(resp + p, REM,
                        "\"research\":{\"domain\":%d,"
                        "\"progress\":%.3f,"
                        "\"ticks_remaining\":%d},",
                        g_pipe_research[i].domain, prog, trem);
                }

                /* Pending hazard threats */
                p += snprintf(resp + p, REM, "\"threats\":[");
                {
                    pending_hazard_t tbuf[8];
                    int tc3 = events_get_threats(&g_pipe_events, pr->id, tbuf, 8);
                    for (int t = 0; t < tc3; t++) {
                        if (t > 0) resp[p++] = ',';
                        int ticks_until = (int)(tbuf[t].strike_tick - uni.tick);
                        if (ticks_until < 0) ticks_until = 0;
                        const char *haz_names[] = {"solar_flare","asteroid_collision","radiation_burst"};
                        const char *hname = (tbuf[t].subtype >= 0 && tbuf[t].subtype < 3)
                            ? haz_names[tbuf[t].subtype] : "unknown";
                        p += snprintf(resp + p, REM,
                            "{\"type\":\"%s\",\"severity\":%.3f,\"ticks_until\":%d}",
                            hname, (double)tbuf[t].severity, ticks_until);
                    }
                }
                p += snprintf(resp + p, REM, "],");

                /* Relay network */
                p += snprintf(resp + p, REM, "\"relay_network\":[");
                {
                    int rc2 = 0;
                    for (int r = 0; r < g_pipe_comm.relay_count; r++) {
                        relay_t *rl = &g_pipe_comm.relays[r];
                        if (!rl->active) continue;
                        if (rc2 > 0) resp[p++] = ',';
                        p += snprintf(resp + p, REM,
                            "{\"system_id\":\"%llu-%llu\","
                            "\"owner\":\"%llu-%llu\","
                            "\"range_ly\":%.1f}",
                            (unsigned long long)rl->system_id.hi,
                            (unsigned long long)rl->system_id.lo,
                            (unsigned long long)rl->owner_id.hi,
                            (unsigned long long)rl->owner_id.lo,
                            rl->range_ly);
                        rc2++;
                    }
                }
                p += snprintf(resp + p, REM, "],");

                /* Close probe object — remove trailing comma if needed */
                if (p > 0 && resp[p-1] == ',') p--;
                p += snprintf(resp + p, REM, "}");
            }
            p += snprintf(resp + p, REM, "]}");
            #undef REM
            fprintf(stdout, "%s\n", resp);
            fflush(stdout);
            continue;
        }

        /* ---- status ---- */
        if (strcmp(cmd, "status") == 0) {
            int p = 0;
            p += snprintf(resp + p, sizeof(resp) - (size_t)p,
                "{\"ok\":true,\"tick\":%llu,\"probes\":[",
                (unsigned long long)uni.tick);
            for (uint32_t i = 0; i < uni.probe_count; i++) {
                if (i > 0) resp[p++] = ',';
                probe_t *pr = &uni.probes[i];
                p += snprintf(resp + p, sizeof(resp) - (size_t)p,
                    "{\"id\":\"%llu-%llu\",\"name\":\"%s\","
                    "\"status\":\"%s\",\"location\":\"%s\","
                    "\"generation\":%u}",
                    (unsigned long long)pr->id.hi,
                    (unsigned long long)pr->id.lo,
                    pr->name,
                    PIPE_STATUS_NAMES[pr->status],
                    PIPE_LOC_NAMES[pr->location_type],
                    pr->generation);
            }
            p += snprintf(resp + p, sizeof(resp) - (size_t)p, "]}");
            fprintf(stdout, "%s\n", resp);
            fflush(stdout);
            continue;
        }

        /* ---- metrics ---- */
        if (strcmp(cmd, "metrics") == 0) {
            metrics_record(&g_pipe_metrics, &uni, &g_pipe_events, uni.tick);
            const metrics_snapshot_t *m = metrics_latest(&g_pipe_metrics);
            if (m) {
                fprintf(stdout,
                    "{\"ok\":true,\"tick\":%llu,\"probes_spawned\":%u,"
                    "\"avg_tech\":%.2f,\"avg_trust\":%.3f,"
                    "\"systems_explored\":%u,\"total_discoveries\":%u,"
                    "\"total_hazards_survived\":%u}\n",
                    (unsigned long long)m->tick, m->probes_spawned,
                    m->avg_tech_level, (double)m->avg_trust,
                    m->systems_explored, m->total_discoveries,
                    m->total_hazards_survived);
            } else {
                fprintf(stdout,
                    "{\"ok\":true,\"tick\":%llu,\"probes_spawned\":%u,"
                    "\"avg_tech\":0,\"avg_trust\":0}\n",
                    (unsigned long long)uni.tick, uni.probe_count);
            }
            fflush(stdout);
            continue;
        }

        /* ---- inject ---- */
        if (strcmp(cmd, "inject") == 0) {
            const char *ev = strstr(line, "\"event\":");
            if (!ev) { pipe_err("missing event"); continue; }
            ev += 8;
            while (*ev == ' ') ev++;
            if (inject_parse_json(&g_pipe_inject, ev) == 0) {
                fprintf(stdout, "{\"ok\":true,\"queued\":%d}\n",
                        g_pipe_inject.count);
            } else {
                pipe_err("invalid event JSON");
            }
            fflush(stdout);
            continue;
        }

        /* ---- snapshot ---- */
        if (strcmp(cmd, "snapshot") == 0) {
            char tag[MAX_SNAPSHOT_TAG];
            if (pipe_parse_tag(line, tag, sizeof(tag)) != 0 || !tag[0]) {
                pipe_err("missing tag"); continue;
            }
            int slot = snap_find(tag);
            if (slot < 0) slot = snap_alloc();
            snapshot_take(&g_pipe_snap[slot], &uni, tag);
            fprintf(stdout,
                "{\"ok\":true,\"snapshot\":\"%s\",\"tick\":%llu}\n",
                tag, (unsigned long long)uni.tick);
            fflush(stdout);
            continue;
        }

        /* ---- restore ---- */
        if (strcmp(cmd, "restore") == 0) {
            char tag[MAX_SNAPSHOT_TAG];
            if (pipe_parse_tag(line, tag, sizeof(tag)) != 0 || !tag[0]) {
                pipe_err("missing tag"); continue;
            }
            int slot = snap_find(tag);
            if (slot < 0) { pipe_err("snapshot not found"); continue; }
            if (snapshot_restore(&g_pipe_snap[slot], &uni) == 0) {
                rng_seed(&rng, uni.seed);
                for (uint64_t t = 0; t < uni.tick; t++) rng_next(&rng);
                fprintf(stdout,
                    "{\"ok\":true,\"restored\":\"%s\",\"tick\":%llu}\n",
                    tag, (unsigned long long)uni.tick);
            } else {
                pipe_err("restore failed");
            }
            fflush(stdout);
            continue;
        }

        /* ---- config ---- */
        if (strcmp(cmd, "config") == 0) {
            const char *data = strstr(line, "\"data\":");
            if (!data) { pipe_err("missing data"); continue; }
            data += 7;
            int n = config_parse_json(&g_pipe_cfg, data);
            fprintf(stdout, "{\"ok\":true,\"entries\":%d}\n", n);
            fflush(stdout);
            continue;
        }

        /* ---- save ---- */
        if (strcmp(cmd, "save") == 0) {
            char path[256] = {0};
            const char *pp = strstr(line, "\"path\":\"");
            if (!pp) { pipe_err("missing path"); continue; }
            pp += 8;
            int pi = 0;
            while (*pp && *pp != '"' && pi < 255) path[pi++] = *pp++;
            path[pi] = '\0';

            persist_t db;
            if (persist_open(&db, path) != 0) {
                pipe_err("db open failed"); continue;
            }
            persist_save_meta(&db, &uni);
            for (uint32_t i = 0; i < uni.probe_count; i++) {
                persist_save_probe(&db, &uni.probes[i]);
            }
            persist_close(&db);
            fprintf(stdout,
                "{\"ok\":true,\"saved\":\"%s\",\"tick\":%llu,\"probes\":%u}\n",
                path, (unsigned long long)uni.tick, uni.probe_count);
            fflush(stdout);
            continue;
        }

        /* ---- load ---- */
        if (strcmp(cmd, "load") == 0) {
            char path[256] = {0};
            const char *pp = strstr(line, "\"path\":\"");
            if (!pp) { pipe_err("missing path"); continue; }
            pp += 8;
            int pi = 0;
            while (*pp && *pp != '"' && pi < 255) path[pi++] = *pp++;
            path[pi] = '\0';

            persist_t db;
            if (persist_open(&db, path) != 0) {
                pipe_err("db open failed"); continue;
            }
            if (persist_load_meta(&db, &uni) != 0) {
                persist_close(&db);
                pipe_err("no meta in db"); continue;
            }
            /* Load probes — query all from probes table */
            uni.probe_count = 0;
            sqlite3_stmt *stmt;
            if (sqlite3_prepare_v2(db.db,
                "SELECT id FROM probes ORDER BY generation, id", -1,
                &stmt, NULL) == SQLITE_OK) {
                while (sqlite3_step(stmt) == SQLITE_ROW && uni.probe_count < MAX_PROBES) {
                    const char *id_hex = (const char *)sqlite3_column_text(stmt, 0);
                    if (id_hex) {
                        probe_uid_t uid = {0, 0};
                        if (strlen(id_hex) == 32) {
                            char hi_s[17] = {0}, lo_s[17] = {0};
                            memcpy(hi_s, id_hex, 16);
                            memcpy(lo_s, id_hex + 16, 16);
                            uid.hi = strtoull(hi_s, NULL, 16);
                            uid.lo = strtoull(lo_s, NULL, 16);
                        }
                        if (persist_load_probe(&db, uid,
                            &uni.probes[uni.probe_count]) == 0) {
                            uni.probe_count++;
                        }
                    }
                }
                sqlite3_finalize(stmt);
            }
            persist_close(&db);
            /* Re-seed RNG to match loaded tick */
            rng_seed(&rng, uni.seed);
            for (uint64_t t = 0; t < uni.tick; t++) rng_next(&rng);
            /* Reset replication/comm/society/research state */
            memset(g_pipe_repl, 0, sizeof(g_pipe_repl));
            memset(g_pipe_research, 0, sizeof(g_pipe_research));
            comm_init(&g_pipe_comm);
            society_init(&g_pipe_society);
            fprintf(stdout,
                "{\"ok\":true,\"loaded\":\"%s\",\"tick\":%llu,\"probes\":%u}\n",
                path, (unsigned long long)uni.tick, uni.probe_count);
            fflush(stdout);
            continue;
        }

        /* ---- scan ---- */
        if (strcmp(cmd, "scan") == 0) {
            /* Parse probe_id from: {"cmd":"scan","probe_id":"1-1"} */
            char pid_str[64] = {0};
            const char *pp = strstr(line, "\"probe_id\":\"");
            if (!pp) { pipe_err("missing probe_id"); continue; }
            pp += 12;
            int pi = 0;
            while (*pp && *pp != '"' && pi < 63) pid_str[pi++] = *pp++;
            pid_str[pi] = '\0';

            probe_uid_t uid = parse_uid_str(pid_str);
            int idx = find_probe_idx(&uni, uid);
            if (idx < 0) { pipe_err("probe not found"); continue; }

            probe_t *pr = &uni.probes[idx];

            /* Generate systems from nearby sectors (3x3x3 cube) */
            system_t nearby[30 * 27]; /* up to 27 sectors × 30 systems */
            int nearby_count = 0;
            sector_coord_t base = pr->sector;
            for (int dx = -1; dx <= 1; dx++) {
                for (int dy = -1; dy <= 1; dy++) {
                    for (int dz = -1; dz <= 1; dz++) {
                        sector_coord_t sc = {
                            base.x + dx, base.y + dy, base.z + dz
                        };
                        int n = generate_sector(
                            &nearby[nearby_count], 30, seed, sc);
                        nearby_count += n;
                        if (nearby_count > 30 * 27 - 30) break;
                    }
                    if (nearby_count > 30 * 27 - 30) break;
                }
                if (nearby_count > 30 * 27 - 30) break;
            }

            /* Run travel_scan */
            scan_result_t results[64];
            int found = travel_scan(pr, nearby, nearby_count,
                                    results, 64);

            /* Build response — include system names and positions */
            int p = 0;
            size_t rem2;
            #define REM2 (rem2 = sizeof(resp) - (size_t)p, rem2)
            p += snprintf(resp + p, REM2,
                "{\"ok\":true,\"probe_id\":\"%s\",\"systems\":[",
                pid_str);
            for (int s = 0; s < found; s++) {
                if (s > 0) resp[p++] = ',';
                /* Find the system name and position from nearby array */
                const char *sname = "unknown";
                vec3_t spos = {0, 0, 0};
                sector_coord_t ssec = {0, 0, 0};
                int star_count = 0;
                star_class_t sclass = results[s].star_class;
                for (int j = 0; j < nearby_count; j++) {
                    if (uid_eq(nearby[j].id, results[s].system_id)) {
                        sname = nearby[j].name;
                        spos = nearby[j].position;
                        ssec = nearby[j].sector;
                        star_count = nearby[j].star_count;
                        sclass = nearby[j].stars[0].class;
                        /* Cache the system for future use */
                        if (g_pipe_sys_count < SYS_CACHE_MAX) {
                            g_pipe_sys_cache[g_pipe_sys_count++] = nearby[j];
                        }
                        break;
                    }
                }
                double est_ticks = results[s].distance_ly
                    / (double)pr->max_speed_c * TICKS_PER_CYCLE;
                p += snprintf(resp + p, REM2,
                    "{\"system_id\":\"%llu-%llu\","
                    "\"name\":\"%s\","
                    "\"star_class\":%d,"
                    "\"star_count\":%d,"
                    "\"distance_ly\":%.3f,"
                    "\"estimated_travel_ticks\":%llu,"
                    "\"position\":[%.3f,%.3f,%.3f],"
                    "\"sector\":[%d,%d,%d]}",
                    (unsigned long long)results[s].system_id.hi,
                    (unsigned long long)results[s].system_id.lo,
                    sname, (int)sclass, star_count,
                    results[s].distance_ly,
                    (unsigned long long)est_ticks,
                    spos.x, spos.y, spos.z,
                    ssec.x, ssec.y, ssec.z);
            }
            p += snprintf(resp + p, REM2, "]}");
            #undef REM2
            fprintf(stdout, "%s\n", resp);
            fflush(stdout);
            continue;
        }

        /* ---- scenario ---- */
        if (strcmp(cmd, "scenario") == 0) {
            /* Parse if body contains "events" array (POST), otherwise GET */
            const char *evts = strstr(line, "\"events\":");
            if (evts) {
                /* Load scenario events: [{"at_tick":N,"type":T,"subtype":S,"severity":F},..] */
                g_pipe_scenario_count = 0;
                const char *arr = strchr(evts, '[');
                if (!arr) { pipe_err("invalid scenario events"); continue; }
                const char *cursor = arr + 1;
                while (*cursor && g_pipe_scenario_count < MAX_SCENARIO_EVENTS) {
                    const char *obj = strchr(cursor, '{');
                    if (!obj) break;
                    scenario_event_t *se = &g_pipe_scenario[g_pipe_scenario_count];
                    memset(se, 0, sizeof(*se));
                    /* Parse at_tick */
                    const char *at = strstr(obj, "\"at_tick\":");
                    if (at) se->at_tick = (uint64_t)atoll(at + 10);
                    /* Parse type */
                    const char *tp = strstr(obj, "\"type\":");
                    if (tp) se->type = (event_type_t)atoi(tp + 7);
                    /* Parse subtype */
                    const char *st = strstr(obj, "\"subtype\":");
                    if (st) se->subtype = atoi(st + 10);
                    /* Parse severity */
                    const char *sv = strstr(obj, "\"severity\":");
                    if (sv) se->severity = (float)atof(sv + 11);
                    /* Parse probe target (optional) */
                    const char *pr = strstr(obj, "\"probe\":\"");
                    if (pr) {
                        pr += 9;
                        char pbuf[64] = {0};
                        int pi2 = 0;
                        while (*pr && *pr != '"' && pi2 < 63) pbuf[pi2++] = *pr++;
                        pbuf[pi2] = '\0';
                        se->target = parse_uid_str(pbuf);
                    }
                    se->fired = false;
                    g_pipe_scenario_count++;
                    cursor = strchr(obj, '}');
                    if (!cursor) break;
                    cursor++;
                }
                fprintf(stdout, "{\"ok\":true,\"loaded\":%d}\n",
                        g_pipe_scenario_count);
            } else {
                /* GET: return current scenario */
                int p = 0;
                p += snprintf(resp + p, sizeof(resp) - (size_t)p,
                    "{\"ok\":true,\"events\":[");
                for (int si = 0; si < g_pipe_scenario_count; si++) {
                    scenario_event_t *se = &g_pipe_scenario[si];
                    if (si > 0) resp[p++] = ',';
                    p += snprintf(resp + p, sizeof(resp) - (size_t)p,
                        "{\"at_tick\":%llu,\"type\":%d,"
                        "\"subtype\":%d,\"severity\":%.3f,"
                        "\"fired\":%s}",
                        (unsigned long long)se->at_tick,
                        (int)se->type, se->subtype,
                        (double)se->severity,
                        se->fired ? "true" : "false");
                }
                p += snprintf(resp + p, sizeof(resp) - (size_t)p, "]}");
                fprintf(stdout, "%s\n", resp);
            }
            fflush(stdout);
            continue;
        }

        /* ---- lineage ---- */
        if (strcmp(cmd, "lineage") == 0) {
            int p = 0;
            p += snprintf(resp + p, sizeof(resp) - (size_t)p,
                "{\"ok\":true,\"entries\":[");
            for (int li = 0; li < g_pipe_lineage.count; li++) {
                lineage_entry_t *e = &g_pipe_lineage.entries[li];
                if (li > 0) resp[p++] = ',';
                p += snprintf(resp + p, sizeof(resp) - (size_t)p,
                    "{\"parent\":\"%llu-%llu\","
                    "\"child\":\"%llu-%llu\","
                    "\"birth_tick\":%llu,"
                    "\"generation\":%u}",
                    (unsigned long long)e->parent_id.hi,
                    (unsigned long long)e->parent_id.lo,
                    (unsigned long long)e->child_id.hi,
                    (unsigned long long)e->child_id.lo,
                    (unsigned long long)e->birth_tick,
                    e->generation);
            }
            p += snprintf(resp + p, sizeof(resp) - (size_t)p, "]}");
            fprintf(stdout, "%s\n", resp);
            fflush(stdout);
            continue;
        }

        /* ---- history ---- */
        if (strcmp(cmd, "history") == 0) {
            /* Parse probe_id */
            char pid_str[64] = {0};
            const char *pp = strstr(line, "\"probe_id\":\"");
            if (!pp) { pipe_err("missing probe_id"); continue; }
            pp += 12;
            int pi2 = 0;
            while (*pp && *pp != '"' && pi2 < 63) pid_str[pi2++] = *pp++;
            pid_str[pi2] = '\0';
            probe_uid_t uid = parse_uid_str(pid_str);

            int p = 0;
            p += snprintf(resp + p, sizeof(resp) - (size_t)p,
                "{\"ok\":true,\"probe_id\":\"%s\",\"events\":[", pid_str);
            int ec = 0;
            for (int ei = 0; ei < g_pipe_events.count; ei++) {
                sim_event_t *ev = &g_pipe_events.events[ei];
                if (!uid_eq(ev->probe_id, uid)) continue;
                if (ec > 0) resp[p++] = ',';
                /* Escape description */
                char desc[512];
                int di = 0;
                for (int c = 0; ev->description[c] && di < 500; c++) {
                    char ch = ev->description[c];
                    if (ch == '"' || ch == '\\') desc[di++] = '\\';
                    desc[di++] = ch;
                }
                desc[di] = '\0';
                p += snprintf(resp + p, sizeof(resp) - (size_t)p,
                    "{\"type\":%d,\"subtype\":%d,"
                    "\"tick\":%llu,\"severity\":%.3f,"
                    "\"description\":\"%s\"}",
                    (int)ev->type, ev->subtype,
                    (unsigned long long)ev->tick,
                    (double)ev->severity, desc);
                ec++;
            }
            p += snprintf(resp + p, sizeof(resp) - (size_t)p, "]}");
            fprintf(stdout, "%s\n", resp);
            fflush(stdout);
            continue;
        }

        pipe_err("unknown command");
    }

    arena_destroy(&arena);
    return 0;
}

/* ---- Main ---- */

int main(int argc, char **argv) {
    cli_config_t cfg = parse_args(argc, argv);

    if (cfg.pipe) return run_pipe_mode(cfg.seed);

    signal(SIGINT, handle_sigint);
    signal(SIGTERM, handle_sigint);

    /* Initialize universe state */
    static universe_t universe; /* static to avoid stack overflow (it's big) */
    memset(&universe, 0, sizeof(universe));
    universe.seed = cfg.seed;
    universe.tick = 0;
    universe.generation_version = 1;
    universe.running = true;
    universe.visual = cfg.visual;

    /* Open database */
    persist_t db;
    if (persist_open(&db, cfg.db_path) != 0) {
        LOG_ERROR("Failed to open database: %s", cfg.db_path);
        return 1;
    }

    /* Resume or start fresh */
    if (cfg.resume) {
        if (persist_load_meta(&db, &universe) == 0) {
            LOG_INFO("Resumed: seed=%llu tick=%llu",
                (unsigned long long)universe.seed,
                (unsigned long long)universe.tick);
        } else {
            LOG_WARN("No existing state found, starting fresh");
        }
    }

    /* Initialize PRNG from seed */
    rng_t rng;
    rng_seed(&rng, universe.seed);
    /* Fast-forward RNG to current tick if resuming */
    for (uint64_t i = 0; i < universe.tick; i++) {
        rng_next(&rng);
    }

    /* Initialize per-tick arena (1 MB) */
    arena_t tick_arena;
    if (arena_init(&tick_arena, 1024 * 1024) != 0) {
        LOG_ERROR("Failed to allocate tick arena");
        persist_close(&db);
        return 1;
    }

    /* Initialize Bob if starting fresh */
    if (!cfg.resume && universe.probe_count == 0) {
        probe_init_bob(&universe.probes[0]);
        universe.probe_count = 1;

        /* Place Bob in the first system of the origin sector */
        system_t origin[30];
        int sys_count = generate_sector(origin, 30, universe.seed,
                                        (sector_coord_t){0, 0, 0});
        if (sys_count > 0) {
            universe.probes[0].system_id = origin[0].id;
            universe.probes[0].sector = origin[0].sector;
            universe.probes[0].heading = origin[0].position;
            universe.probes[0].location_type = LOC_IN_SYSTEM;
            persist_save_sector(&db, (sector_coord_t){0, 0, 0},
                                universe.tick, origin, sys_count);
        }
    }

    /* Save initial metadata */
    if (!cfg.resume) {
        persist_save_meta(&db, &universe);
    }

    LOG_INFO("Project UNIVERSE");
    LOG_INFO("  Seed:    %llu", (unsigned long long)universe.seed);
    LOG_INFO("  Mode:    %s", cfg.visual ? "visual" : "headless");
    LOG_INFO("  Probes:  %u", universe.probe_count);
    if (cfg.max_ticks > 0) {
        LOG_INFO("  Target:  %llu ticks", (unsigned long long)cfg.max_ticks);
    } else {
        LOG_INFO("  Target:  unlimited (Ctrl-C to stop)");
    }

#ifdef USE_RAYLIB
    /* Initialize Raylib renderer */
    renderer_t renderer;
    if (cfg.visual) {
        renderer_init(&renderer, 1280, 800, universe.seed);
        sim_speed_init_target(&renderer.speed, cfg.sim_years, cfg.real_hours, 60);
        LOG_INFO("  Speed:   %s (%.1f sim-years in %.1f hours)",
            sim_speed_label(&renderer.speed), cfg.sim_years, cfg.real_hours);
        if (universe.probe_count > 0) {
            renderer_load_nearby(&renderer, &universe.probes[0]);
        }
    }
#else
    if (cfg.visual) {
        LOG_WARN("Built without Raylib. Use 'make visual' for --visual support.");
        LOG_WARN("Falling back to headless mode.");
        cfg.visual = false;
    }
#endif

    /* ---- Main simulation loop ---- */

    struct timespec t_start, t_end;
    clock_gettime(CLOCK_MONOTONIC, &t_start);

    while (g_running) {
#ifdef USE_RAYLIB
        if (cfg.visual) {
            if (!renderer_update(&renderer, &universe)) {
                g_running = 0;
                break;
            }

            /* Run N ticks per frame based on speed control */
            int ticks_this_frame = sim_speed_ticks_this_frame(&renderer.speed);
            for (int t = 0; t < ticks_this_frame; t++) {
                universe.tick++;

                /* Reset per-tick arena */
                arena_reset(&tick_arena);

                /* Advance RNG */
                uint64_t tick_entropy = rng_next(&rng);
                (void)tick_entropy;

                /* Travel ticks for active probes */
                for (uint32_t p = 0; p < universe.probe_count; p++) {
                    if (universe.probes[p].status == STATUS_TRAVELING) {
                        travel_tick(&universe.probes[p], &rng);
                    }
                    probe_tick_energy(&universe.probes[p]);
                }

                /* Periodic save */
                if (universe.tick % cfg.save_interval == 0) {
                    persist_save_tick(&db, universe.tick);
                }

                if (cfg.max_ticks > 0 && universe.tick >= cfg.max_ticks) {
                    g_running = 0;
                    break;
                }
            }

            /* Reload visible systems periodically */
            if (universe.probe_count > 0 && universe.tick % 100 == 0) {
                renderer_load_nearby(&renderer, &universe.probes[0]);
            }

            renderer_draw(&renderer, &universe);
        } else
#endif
        {
            /* Headless mode — run as fast as possible */
            universe.tick++;
            arena_reset(&tick_arena);

            uint64_t tick_entropy = rng_next(&rng);
            (void)tick_entropy;

            /* Travel ticks */
            for (uint32_t p = 0; p < universe.probe_count; p++) {
                if (universe.probes[p].status == STATUS_TRAVELING) {
                    travel_tick(&universe.probes[p], &rng);
                }
                probe_tick_energy(&universe.probes[p]);
            }

            /* Periodic save */
            if (universe.tick % cfg.save_interval == 0) {
                persist_save_tick(&db, universe.tick);
            }

            if (cfg.max_ticks > 0 && universe.tick >= cfg.max_ticks) {
                break;
            }
        }
    }

    clock_gettime(CLOCK_MONOTONIC, &t_end);
    double elapsed = (t_end.tv_sec - t_start.tv_sec) +
                     (t_end.tv_nsec - t_start.tv_nsec) / 1e9;

    /* Final save */
    persist_save_meta(&db, &universe);
    for (uint32_t p = 0; p < universe.probe_count; p++) {
        persist_save_probe(&db, &universe.probes[p]);
    }

    LOG_INFO("Simulation ended at tick %llu (%.3f seconds, %.0f ticks/sec)",
        (unsigned long long)universe.tick, elapsed,
        elapsed > 0 ? universe.tick / elapsed : 0);

#ifdef USE_RAYLIB
    if (cfg.visual) {
        renderer_close(&renderer);
    }
#endif

    /* Cleanup */
    arena_destroy(&tick_arena);
    persist_close(&db);

    return 0;
}
