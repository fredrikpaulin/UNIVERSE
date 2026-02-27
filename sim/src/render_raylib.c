/*
 * render_raylib.c — Raylib visualization for Project UNIVERSE
 *
 * Three views:
 *   1. Galaxy map — 2D starfield, sectors, probe marker + trail
 *   2. System view — star at center, orbital ellipses, planets, probe
 *   3. Probe dashboard — status bars, personality radar, resource levels
 *
 * Keyboard:
 *   Space  = pause/unpause
 *   +/-    = speed up/down
 *   Tab    = cycle views
 *   Escape = back / close help
 *   H      = toggle help overlay
 *   F      = toggle fullscreen
 *
 * Mouse:
 *   Scroll        = zoom
 *   Left drag     = pan
 *   Left click    = select system/planet
 *   Right click   = deselect / back
 */
#ifdef USE_RAYLIB

#include "render_raylib.h"
#include "render.h"
#include "probe.h"
#include "generate.h"
#include "util.h"
#include "raylib.h"

#include <stdio.h>
#include <string.h>
#include <math.h>

/* ---- Helpers ---- */

static Color rgba_to_color(rgba_t c) {
    return (Color){c.r, c.g, c.b, c.a};
}

static Color color_alpha(Color c, unsigned char a) {
    return (Color){c.r, c.g, c.b, a};
}

/* ---- Init / Close ---- */

void renderer_init(renderer_t *r, int width, int height, uint64_t galaxy_seed) {
    memset(r, 0, sizeof(*r));
    r->screen_w = width;
    r->screen_h = height;
    r->galaxy_seed = galaxy_seed;
    r->hovered_planet = -1;

    view_state_init(&r->view);
    sim_speed_init(&r->speed);
    probe_trail_init(&r->trail);

    /* Galaxy camera: centered on screen, 1 px = 2 ly */
    r->galaxy_cam.offset_x = width / 2.0;
    r->galaxy_cam.offset_y = height / 2.0;
    r->galaxy_cam.scale = 2.0;

    /* System camera: centered, 1 px = 50 AU */
    r->system_cam.offset_x = width / 2.0;
    r->system_cam.offset_y = height / 2.0;
    r->system_cam.scale = 50.0;

    SetConfigFlags(FLAG_WINDOW_RESIZABLE | FLAG_MSAA_4X_HINT);
    InitWindow(width, height, "Project UNIVERSE");
    SetTargetFPS(60);
}

void renderer_close(renderer_t *r) {
    (void)r;
    CloseWindow();
}

/* ---- Load nearby systems ---- */

void renderer_load_nearby(renderer_t *r, const probe_t *probe) {
    r->visible_system_count = 0;
    sector_coord_t base = probe->sector;

    /* Load 3x3x1 sector neighborhood */
    for (int dx = -1; dx <= 1; dx++) {
        for (int dy = -1; dy <= 1; dy++) {
            sector_coord_t sc = {base.x + dx, base.y + dy, base.z};
            system_t buf[30];
            int n = generate_sector(buf, 30, r->galaxy_seed, sc);
            for (int i = 0; i < n && r->visible_system_count < 256; i++) {
                r->visible_systems[r->visible_system_count++] = buf[i];
            }
        }
    }
}

/* ---- Input handling ---- */

bool renderer_update(renderer_t *r, universe_t *u) {
    if (WindowShouldClose()) return false;

    /* Handle resize */
    if (IsWindowResized()) {
        r->screen_w = GetScreenWidth();
        r->screen_h = GetScreenHeight();
        r->galaxy_cam.offset_x = r->screen_w / 2.0;
        r->galaxy_cam.offset_y = r->screen_h / 2.0;
        r->system_cam.offset_x = r->screen_w / 2.0;
        r->system_cam.offset_y = r->screen_h / 2.0;
    }

    /* Keyboard */
    if (IsKeyPressed(KEY_SPACE)) sim_speed_toggle_pause(&r->speed);
    if (IsKeyPressed(KEY_EQUAL) || IsKeyPressed(KEY_KP_ADD)) sim_speed_up(&r->speed);
    if (IsKeyPressed(KEY_MINUS) || IsKeyPressed(KEY_KP_SUBTRACT)) sim_speed_down(&r->speed);
    if (IsKeyPressed(KEY_H)) r->show_help = !r->show_help;
    if (IsKeyPressed(KEY_F)) ToggleFullscreen();

    if (IsKeyPressed(KEY_TAB)) {
        /* Cycle views */
        if (r->view.current_view == VIEW_GALAXY && u->probe_count > 0) {
            view_state_select_probe(&r->view, u->probes[0].id);
        } else if (r->view.current_view == VIEW_PROBE) {
            view_state_back(&r->view);
            if (r->view.current_view == VIEW_GALAXY) {
                /* Try to go to system view */
                if (u->probe_count > 0) {
                    view_state_select_system(&r->view, u->probes[0].system_id);
                }
            }
        } else {
            view_state_back(&r->view);
        }
    }

    if (IsKeyPressed(KEY_ESCAPE)) {
        if (r->show_help) {
            r->show_help = false;
        } else {
            view_state_back(&r->view);
        }
    }

    /* Mouse zoom */
    camera_2d_t *cam = (r->view.current_view == VIEW_SYSTEM)
                        ? &r->system_cam : &r->galaxy_cam;
    float wheel = GetMouseWheelMove();
    if (wheel != 0) {
        double factor = (wheel > 0) ? 1.15 : 1.0 / 1.15;
        camera_zoom(cam, factor);
    }

    /* Mouse drag to pan */
    if (IsMouseButtonDown(MOUSE_BUTTON_LEFT) &&
        (fabsf(GetMouseDelta().x) > 1 || fabsf(GetMouseDelta().y) > 1)) {
        Vector2 delta = GetMouseDelta();
        cam->offset_x += delta.x;
        cam->offset_y += delta.y;
    }

    /* Left click to select */
    if (IsMouseButtonPressed(MOUSE_BUTTON_LEFT)) {
        Vector2 mouse = GetMousePosition();

        if (r->view.current_view == VIEW_GALAXY) {
            probe_uid_t hit = hit_test_system(r->visible_systems,
                r->visible_system_count, &r->galaxy_cam,
                mouse.x, mouse.y, 15.0);
            if (!uid_is_null(hit)) {
                view_state_select_system(&r->view, hit);
            }
        } else if (r->view.current_view == VIEW_SYSTEM) {
            /* Check planet clicks */
            r->hovered_planet = -1;
            for (int i = 0; i < 256; i++) {
                if (!uid_eq(r->visible_systems[i].id, r->view.selected_system))
                    continue;
                system_t *sys = &r->visible_systems[i];
                for (int p = 0; p < sys->planet_count; p++) {
                    double px, py;
                    planet_orbital_pos(&sys->planets[p], u->tick, &px, &py);
                    double sx, sy;
                    world_to_screen(&r->system_cam, px, py, &sx, &sy);
                    double dx = sx - mouse.x;
                    double dy = sy - mouse.y;
                    if (dx*dx + dy*dy < 15.0*15.0) {
                        view_state_select_planet(&r->view, sys->planets[p].id);
                        r->hovered_planet = p;
                    }
                }
                break;
            }
        }
    }

    /* Right click = back */
    if (IsMouseButtonPressed(MOUSE_BUTTON_RIGHT)) {
        view_state_back(&r->view);
    }

    /* Update probe trail */
    if (u->probe_count > 0) {
        probe_trail_push(&r->trail, u->probes[0].heading);
    }

    return true;
}

/* ---- Drawing: Galaxy View ---- */

static void draw_galaxy(renderer_t *r, const universe_t *u) {
    camera_2d_t *cam = &r->galaxy_cam;

    /* Draw grid lines */
    double sector_size = 100.0; /* ly per sector */
    double wx0, wy0, wx1, wy1;
    screen_to_world(cam, 0, 0, &wx0, &wy0);
    screen_to_world(cam, r->screen_w, r->screen_h, &wx1, &wy1);

    double grid_start_x = floor(wx0 / sector_size) * sector_size;
    double grid_start_y = floor(wy0 / sector_size) * sector_size;

    for (double gx = grid_start_x; gx <= wx1; gx += sector_size) {
        double sx, sy1, sy2;
        world_to_screen(cam, gx, wy0, &sx, &sy1);
        world_to_screen(cam, gx, wy1, &sx, &sy2);
        DrawLine((int)sx, (int)sy1, (int)sx, (int)sy2,
                 (Color){40, 40, 60, 255});
    }
    for (double gy = grid_start_y; gy <= wy1; gy += sector_size) {
        double sx1, sx2, sy;
        world_to_screen(cam, wx0, gy, &sx1, &sy);
        world_to_screen(cam, wx1, gy, &sx2, &sy);
        DrawLine((int)sx1, (int)sy, (int)sx2, (int)sy,
                 (Color){40, 40, 60, 255});
    }

    /* Draw stars */
    for (int i = 0; i < r->visible_system_count; i++) {
        system_t *sys = &r->visible_systems[i];
        double sx, sy;
        world_to_screen(cam, sys->position.x, sys->position.y, &sx, &sy);

        rgba_t col = star_class_color(sys->stars[0].class);
        Color c = rgba_to_color(col);
        int radius = 3;

        /* Larger dots for brighter stars */
        if (sys->stars[0].class <= STAR_A) radius = 5;
        else if (sys->stars[0].class <= STAR_F) radius = 4;

        /* Visited systems are brighter */
        if (sys->visited) {
            DrawCircle((int)sx, (int)sy, radius + 1, color_alpha(c, 60));
        }

        DrawCircle((int)sx, (int)sy, radius, c);

        /* Label if zoomed in enough */
        if (cam->scale > 3.0) {
            DrawText(sys->name, (int)sx + radius + 3, (int)sy - 5, 10,
                     color_alpha(RAYWHITE, 150));
        }
    }

    /* Draw probe marker + trail */
    if (u->probe_count > 0) {
        const probe_t *bob = &u->probes[0];

        /* Trail */
        for (int i = 1; i < r->trail.count; i++) {
            vec3_t p0 = probe_trail_get(&r->trail, i - 1);
            vec3_t p1 = probe_trail_get(&r->trail, i);
            double sx0, sy0, sx1, sy1;
            world_to_screen(cam, p0.x, p0.y, &sx0, &sy0);
            world_to_screen(cam, p1.x, p1.y, &sx1, &sy1);
            unsigned char alpha = (unsigned char)(80 + 175 * i / r->trail.count);
            DrawLine((int)sx0, (int)sy0, (int)sx1, (int)sy1,
                     (Color){100, 200, 255, alpha});
        }

        /* Probe dot */
        double px, py;
        world_to_screen(cam, bob->heading.x, bob->heading.y, &px, &py);
        DrawCircle((int)px, (int)py, 6, (Color){100, 255, 100, 255});
        DrawCircle((int)px, (int)py, 4, (Color){200, 255, 200, 255});
        DrawText(bob->name, (int)px + 10, (int)py - 5, 12, GREEN);
    }
}

/* ---- Drawing: System View ---- */

static system_t *find_selected_system(renderer_t *r) {
    for (int i = 0; i < r->visible_system_count; i++) {
        if (uid_eq(r->visible_systems[i].id, r->view.selected_system))
            return &r->visible_systems[i];
    }
    return NULL;
}

static void draw_system(renderer_t *r, const universe_t *u) {
    camera_2d_t *cam = &r->system_cam;
    system_t *sys = find_selected_system(r);
    if (!sys) {
        DrawText("System not loaded", 20, 40, 20, RED);
        return;
    }

    /* Star at center */
    rgba_t star_col = star_class_color(sys->stars[0].class);
    double cx, cy;
    world_to_screen(cam, 0, 0, &cx, &cy);

    int star_r = (int)(12 * cam->scale / 50.0);
    if (star_r < 6) star_r = 6;
    if (star_r > 40) star_r = 40;
    Color sc = rgba_to_color(star_col);
    DrawCircle((int)cx, (int)cy, star_r + 4, color_alpha(sc, 40));
    DrawCircle((int)cx, (int)cy, star_r, sc);

    /* Star label */
    DrawText(sys->name, (int)cx + star_r + 5, (int)cy - 8, 14, RAYWHITE);
    char info[128];
    snprintf(info, sizeof(info), "%s  %.2f M☉  %.0f K",
             star_class_name(sys->stars[0].class),
             sys->stars[0].mass_solar,
             sys->stars[0].temperature_k);
    DrawText(info, (int)cx + star_r + 5, (int)cy + 8, 10, GRAY);

    /* Draw planets and orbits */
    for (int i = 0; i < sys->planet_count; i++) {
        planet_t *pl = &sys->planets[i];

        /* Orbital ring */
        double orbit_px = pl->orbital_radius_au * cam->scale;
        DrawCircleLines((int)cx, (int)cy, (int)orbit_px,
                        (Color){50, 50, 70, 255});

        /* Planet position */
        double px, py;
        planet_orbital_pos(pl, u->tick, &px, &py);
        double spx, spy;
        world_to_screen(cam, px, py, &spx, &spy);

        /* Planet dot — size by mass */
        int pr = 3 + (int)(pl->mass_earth * 0.5);
        if (pr > 12) pr = 12;

        /* Color by type */
        Color planet_col;
        switch (pl->type) {
            case PLANET_GAS_GIANT:   planet_col = (Color){200, 150, 100, 255}; break;
            case PLANET_ICE_GIANT:   planet_col = (Color){100, 180, 220, 255}; break;
            case PLANET_OCEAN:       planet_col = (Color){40, 100, 200, 255}; break;
            case PLANET_LAVA:        planet_col = (Color){255, 80, 30, 255}; break;
            case PLANET_ICE:         planet_col = (Color){200, 220, 255, 255}; break;
            case PLANET_DESERT:      planet_col = (Color){210, 180, 100, 255}; break;
            default:                 planet_col = (Color){160, 160, 160, 255}; break;
        }

        /* Highlight selected planet */
        bool selected = uid_eq(pl->id, r->view.selected_planet);
        if (selected) {
            DrawCircle((int)spx, (int)spy, pr + 3, (Color){255, 255, 100, 80});
        }

        DrawCircle((int)spx, (int)spy, pr, planet_col);

        /* Label */
        if (cam->scale > 20.0 || selected) {
            DrawText(pl->name, (int)spx + pr + 3, (int)spy - 5, 10, RAYWHITE);
        }
    }

    /* Selected planet info panel */
    if (r->hovered_planet >= 0 && r->hovered_planet < sys->planet_count) {
        planet_t *pl = &sys->planets[r->hovered_planet];
        int panel_x = r->screen_w - 260;
        int panel_y = 60;

        DrawRectangle(panel_x - 10, panel_y - 10, 260, 240, (Color){20, 20, 30, 220});
        DrawRectangleLines(panel_x - 10, panel_y - 10, 260, 240, (Color){80, 80, 120, 255});

        DrawText(pl->name, panel_x, panel_y, 16, RAYWHITE);
        panel_y += 22;
        char buf[128];
        snprintf(buf, sizeof(buf), "Type: %s", planet_type_name(pl->type));
        DrawText(buf, panel_x, panel_y, 12, GRAY); panel_y += 16;
        snprintf(buf, sizeof(buf), "Mass: %.2f Earth", pl->mass_earth);
        DrawText(buf, panel_x, panel_y, 12, GRAY); panel_y += 16;
        snprintf(buf, sizeof(buf), "Orbit: %.2f AU", pl->orbital_radius_au);
        DrawText(buf, panel_x, panel_y, 12, GRAY); panel_y += 16;
        snprintf(buf, sizeof(buf), "Temp: %.0f K", pl->surface_temp_k);
        DrawText(buf, panel_x, panel_y, 12, GRAY); panel_y += 16;
        snprintf(buf, sizeof(buf), "Habitability: %.0f%%", pl->habitability_index * 100);
        DrawText(buf, panel_x, panel_y, 12,
                 pl->habitability_index > 0.5 ? GREEN : GRAY);
        panel_y += 16;
        snprintf(buf, sizeof(buf), "Water: %.0f%%", pl->water_coverage * 100);
        DrawText(buf, panel_x, panel_y, 12, GRAY); panel_y += 16;
        snprintf(buf, sizeof(buf), "Atm: %.2f atm", pl->atmosphere_pressure_atm);
        DrawText(buf, panel_x, panel_y, 12, GRAY); panel_y += 20;

        /* Survey status */
        DrawText("Survey:", panel_x, panel_y, 12, RAYWHITE); panel_y += 16;
        for (int lv = 0; lv < 5; lv++) {
            snprintf(buf, sizeof(buf), "  L%d: %s", lv,
                     pl->surveyed[lv] ? "done" : "---");
            DrawText(buf, panel_x, panel_y, 10,
                     pl->surveyed[lv] ? GREEN : DARKGRAY);
            panel_y += 12;
        }
    }

    /* Draw probe in system */
    if (u->probe_count > 0) {
        const probe_t *bob = &u->probes[0];
        if (uid_eq(bob->system_id, r->view.selected_system)) {
            double bx = 0, by = 0;
            /* If orbiting or landed on a body, draw near that body */
            if (bob->location_type == LOC_ORBITING || bob->location_type == LOC_LANDED) {
                for (int i = 0; i < sys->planet_count; i++) {
                    if (uid_eq(sys->planets[i].id, bob->body_id)) {
                        planet_orbital_pos(&sys->planets[i], u->tick, &bx, &by);
                        if (bob->location_type == LOC_ORBITING) {
                            bx += 0.05; /* slight offset */
                        }
                        break;
                    }
                }
            }
            double spx, spy;
            world_to_screen(cam, bx, by, &spx, &spy);
            DrawCircle((int)spx, (int)spy, 5, GREEN);
            DrawText("Bob", (int)spx + 8, (int)spy - 5, 10, GREEN);
        }
    }
}

/* ---- Drawing: Probe Dashboard ---- */

static void draw_bar(int x, int y, int w, int h, double value, double max,
                     Color fill, const char *label) {
    double frac = (max > 0) ? value / max : 0;
    if (frac > 1.0) frac = 1.0;
    if (frac < 0.0) frac = 0.0;

    DrawRectangle(x, y, w, h, (Color){30, 30, 40, 255});
    DrawRectangle(x, y, (int)(w * frac), h, fill);
    DrawRectangleLines(x, y, w, h, (Color){80, 80, 100, 255});

    char buf[128];
    snprintf(buf, sizeof(buf), "%s: %.0f / %.0f", label, value, max);
    DrawText(buf, x + 4, y + 2, h - 4, RAYWHITE);
}

static void draw_probe_dashboard(renderer_t *r __attribute__((unused)), const universe_t *u) {
    if (u->probe_count == 0) {
        DrawText("No probes active", 20, 40, 20, RED);
        return;
    }

    const probe_t *bob = &u->probes[0];
    int x = 30, y = 60;
    int bar_w = 300, bar_h = 20;

    /* Name and generation */
    char title[128];
    snprintf(title, sizeof(title), "%s (Gen %u)", bob->name, bob->generation);
    DrawText(title, x, y, 24, GREEN); y += 32;

    /* Status */
    const char *status_names[] = {
        "Active", "Traveling", "Mining", "Building",
        "Replicating", "Dormant", "Damaged", "Destroyed"
    };
    Color status_colors[] = {
        GREEN, SKYBLUE, ORANGE, YELLOW,
        PURPLE, GRAY, RED, DARKGRAY
    };
    DrawText(status_names[bob->status], x, y, 16,
             status_colors[bob->status]); y += 24;

    /* Location */
    const char *loc_names[] = {"Interstellar", "In System", "Orbiting", "Landed", "Docked"};
    char loc[128];
    snprintf(loc, sizeof(loc), "Location: %s", loc_names[bob->location_type]);
    DrawText(loc, x, y, 14, GRAY); y += 20;

    if (bob->status == STATUS_TRAVELING) {
        char travel[128];
        snprintf(travel, sizeof(travel), "Speed: %.2fc  Remaining: %.1f ly",
                 bob->speed_c, bob->travel_remaining_ly);
        DrawText(travel, x, y, 14, SKYBLUE); y += 20;
    }
    y += 10;

    /* Resource bars */
    draw_bar(x, y, bar_w, bar_h, bob->hull_integrity * 100, 100,
             bob->hull_integrity > 0.5 ? GREEN : RED, "Hull"); y += 26;
    draw_bar(x, y, bar_w, bar_h, bob->fuel_kg, 50000,
             (Color){100, 180, 255, 255}, "Fuel (kg)"); y += 26;
    draw_bar(x, y, bar_w, bar_h, bob->energy_joules / 1e9, 1000,
             YELLOW, "Energy (GJ)"); y += 36;

    /* Tech levels */
    DrawText("Tech Levels:", x, y, 14, RAYWHITE); y += 18;
    const char *tech_names[] = {
        "Propulsion", "Sensors", "Mining", "Construction",
        "Computing", "Energy", "Materials", "Comms", "Weapons", "Biotech"
    };
    for (int i = 0; i < TECH_COUNT; i++) {
        int level = bob->tech_levels[i];
        char buf[64];
        snprintf(buf, sizeof(buf), "  %s: %d", tech_names[i], level);
        DrawText(buf, x, y, 12, (level > 0) ? SKYBLUE : DARKGRAY);
        /* Mini bar */
        DrawRectangle(x + 160, y + 2, level * 12, 10, SKYBLUE);
        y += 14;
    }
    y += 10;

    /* Personality traits (right column) */
    int rx = 380, ry = 60;
    DrawText("Personality:", rx, ry, 16, RAYWHITE); ry += 22;

    struct { const char *name; float value; } traits[] = {
        {"Curiosity",    bob->personality.curiosity},
        {"Caution",      bob->personality.caution},
        {"Sociability",  bob->personality.sociability},
        {"Humor",        bob->personality.humor},
        {"Empathy",      bob->personality.empathy},
        {"Ambition",     bob->personality.ambition},
        {"Creativity",   bob->personality.creativity},
        {"Stubbornness", bob->personality.stubbornness},
        {"Angst",        bob->personality.existential_angst},
        {"Nostalgia",    bob->personality.nostalgia_for_earth},
    };
    for (int i = 0; i < 10; i++) {
        char buf[64];
        snprintf(buf, sizeof(buf), "%-13s", traits[i].name);
        DrawText(buf, rx, ry, 12, GRAY);
        int bw = (int)(traits[i].value * 120);
        Color bc = (traits[i].value > 0.7f) ? GREEN :
                   (traits[i].value > 0.3f) ? YELLOW : RED;
        DrawRectangle(rx + 110, ry + 2, bw, 10, bc);
        DrawRectangleLines(rx + 110, ry + 2, 120, 10, DARKGRAY);
        ry += 16;
    }
    ry += 10;

    /* Stored resources */
    DrawText("Resources:", rx, ry, 14, RAYWHITE); ry += 18;
    const char *res_names[] = {
        "Iron", "Silicon", "Rare Earth", "Water",
        "Hydrogen", "Helium-3", "Carbon", "Uranium", "Exotic"
    };
    for (int i = 0; i < RES_COUNT; i++) {
        if (bob->resources[i] > 0.01) {
            char buf[64];
            snprintf(buf, sizeof(buf), "  %s: %.0f kg", res_names[i], bob->resources[i]);
            DrawText(buf, rx, ry, 12, SKYBLUE);
            ry += 14;
        }
    }
}

/* ---- Drawing: HUD (always visible) ---- */

static void draw_hud(renderer_t *r, const universe_t *u) {
    /* Top bar */
    DrawRectangle(0, 0, r->screen_w, 32, (Color){15, 15, 25, 230});

    char tick_str[128];
    double years = (double)u->tick / TICKS_PER_CYCLE;
    snprintf(tick_str, sizeof(tick_str),
             "Tick: %llu  (%.1f years)   Speed: %s%s   [%s]",
             (unsigned long long)u->tick, years,
             r->speed.paused ? "PAUSED " : "",
             sim_speed_label(&r->speed),
             r->view.current_view == VIEW_GALAXY ? "Galaxy" :
             r->view.current_view == VIEW_SYSTEM ? "System" : "Probe");
    DrawText(tick_str, 10, 8, 14, RAYWHITE);

    /* FPS */
    char fps[32];
    snprintf(fps, sizeof(fps), "FPS: %d", GetFPS());
    DrawText(fps, r->screen_w - 80, 8, 14, GRAY);

    /* Help hint */
    DrawText("H=Help  Tab=View  Space=Pause  +/-=Speed  Esc=Back",
             10, r->screen_h - 20, 10, DARKGRAY);

    /* Help overlay */
    if (r->show_help) {
        int hw = 400, hh = 300;
        int hx = (r->screen_w - hw) / 2;
        int hy = (r->screen_h - hh) / 2;

        DrawRectangle(hx, hy, hw, hh, (Color){10, 10, 20, 240});
        DrawRectangleLines(hx, hy, hw, hh, SKYBLUE);

        int tx = hx + 20, ty = hy + 20;
        DrawText("Project UNIVERSE — Controls", tx, ty, 18, GREEN); ty += 30;
        DrawText("Space        Pause / Unpause", tx, ty, 13, RAYWHITE); ty += 18;
        DrawText("+  -         Speed up / down", tx, ty, 13, RAYWHITE); ty += 18;
        DrawText("Tab          Cycle views", tx, ty, 13, RAYWHITE); ty += 18;
        DrawText("Escape       Go back", tx, ty, 13, RAYWHITE); ty += 18;
        DrawText("H            Toggle this help", tx, ty, 13, RAYWHITE); ty += 18;
        DrawText("F            Toggle fullscreen", tx, ty, 13, RAYWHITE); ty += 18;
        DrawText("Mouse wheel  Zoom in/out", tx, ty, 13, RAYWHITE); ty += 18;
        DrawText("Left drag    Pan camera", tx, ty, 13, RAYWHITE); ty += 18;
        DrawText("Left click   Select system/planet", tx, ty, 13, RAYWHITE); ty += 18;
        DrawText("Right click  Go back", tx, ty, 13, RAYWHITE); ty += 30;
        DrawText("Press H or Escape to close", tx, ty, 12, GRAY);
    }
}

/* ---- Main draw dispatch ---- */

void renderer_draw(renderer_t *r, const universe_t *u) {
    BeginDrawing();
    ClearBackground((Color){8, 8, 16, 255});

    switch (r->view.current_view) {
        case VIEW_GALAXY: draw_galaxy(r, u); break;
        case VIEW_SYSTEM: draw_system(r, u); break;
        case VIEW_PROBE:  draw_probe_dashboard(r, u); break;
        default: break;
    }

    draw_hud(r, u);
    EndDrawing();
}

#endif /* USE_RAYLIB */
