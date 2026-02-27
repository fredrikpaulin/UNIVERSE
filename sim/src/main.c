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
                system_t *sys = sys_cache_get(uni.probes[i].system_id,
                                              seed, uni.probes[i].sector);
                if (sys) probe_execute_action(&uni.probes[i], &actions[i], sys);
            }

            /* Advance simulation */
            uni.tick++;
            arena_reset(&arena);
            rng_next(&rng);

            for (uint32_t i = 0; i < uni.probe_count; i++) {
                if (uni.probes[i].status == STATUS_TRAVELING)
                    travel_tick(&uni.probes[i], &rng);
                probe_tick_energy(&uni.probes[i]);
            }

            /* Events */
            for (uint32_t i = 0; i < uni.probe_count; i++) {
                if (uni.probes[i].status == STATUS_DESTROYED) continue;
                system_t *sys = sys_cache_get(uni.probes[i].system_id,
                                              seed, uni.probes[i].sector);
                if (sys) events_tick_probe(&g_pipe_events, &uni.probes[i],
                                           sys, uni.tick, &rng);
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
            p += snprintf(resp + p, sizeof(resp) - (size_t)p,
                "{\"ok\":true,\"tick\":%llu,\"observations\":[",
                (unsigned long long)uni.tick);

            for (uint32_t i = 0; i < uni.probe_count; i++) {
                if (i > 0) resp[p++] = ',';
                probe_t *pr = &uni.probes[i];
                p += snprintf(resp + p, sizeof(resp) - (size_t)p,
                    "{\"probe_id\":\"%llu-%llu\","
                    "\"name\":\"%s\","
                    "\"status\":\"%s\","
                    "\"hull\":%.3f,"
                    "\"energy\":%.1f,"
                    "\"fuel\":%.1f,"
                    "\"location\":\"%s\","
                    "\"generation\":%u,"
                    "\"tech\":[%u,%u,%u,%u,%u,%u,%u,%u,%u,%u]}",
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
            }
            p += snprintf(resp + p, sizeof(resp) - (size_t)p, "]}");
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
