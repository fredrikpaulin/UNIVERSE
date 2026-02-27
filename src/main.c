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
} config_t;

static config_t parse_args(int argc, char **argv) {
    config_t cfg = {
        .seed          = 42,
        .max_ticks     = 0,
        .visual        = false,
        .resume        = false,
        .db_path       = "universe.db",
        .save_interval = 100,
        .sim_years     = 24.0,
        .real_hours    = 3.0,
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
        } else if (strcmp(argv[i], "--sim-years") == 0 && i + 1 < argc) {
            cfg.sim_years = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--hours") == 0 && i + 1 < argc) {
            cfg.real_hours = strtod(argv[++i], NULL);
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            printf("Usage: %s [--seed N] [--ticks N] [--headless|--visual] "
                   "[--db PATH] [--save-interval N] [--resume] "
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

/* ---- Main ---- */

int main(int argc, char **argv) {
    config_t cfg = parse_args(argc, argv);

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
