/*
 * render.c — Render logic layer (pure functions, no Raylib dependency)
 *
 * Star colors, view state, speed control, camera math, hit testing,
 * probe trail, orbital position, display name lookups.
 */
#include "render.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Star spectral class → RGB color ---- */

/* Real-ish stellar colors from astrophysics:
 * O = deep blue, B = blue-white, A = white-blue, F = white,
 * G = yellow, K = orange, M = red, WD = white, NS = cyan, BH = dark purple */

static const rgba_t STAR_COLORS[STAR_CLASS_COUNT] = {
    [STAR_O]           = {100, 140, 255, 255},  /* deep blue */
    [STAR_B]           = {160, 190, 255, 255},  /* blue-white */
    [STAR_A]           = {200, 210, 255, 255},  /* white-blue */
    [STAR_F]           = {240, 240, 240, 255},  /* white */
    [STAR_G]           = {255, 240, 150, 255},  /* yellow */
    [STAR_K]           = {255, 180, 80,  255},  /* orange */
    [STAR_M]           = {255, 100, 60,  255},  /* red */
    [STAR_WHITE_DWARF] = {220, 220, 240, 255},  /* bluish white */
    [STAR_NEUTRON]     = {100, 240, 240, 255},  /* cyan pulse */
    [STAR_BLACK_HOLE]  = {30,  10,  40,  255},  /* near-black purple */
};

rgba_t star_class_color(star_class_t class) {
    if ((int)class >= 0 && (int)class < STAR_CLASS_COUNT)
        return STAR_COLORS[(int)class];
    return (rgba_t){128, 128, 128, 255};
}

/* ---- Star class display names ---- */

static const char *STAR_CLASS_NAMES[STAR_CLASS_COUNT] = {
    [STAR_O]           = "O",
    [STAR_B]           = "B",
    [STAR_A]           = "A",
    [STAR_F]           = "F",
    [STAR_G]           = "G",
    [STAR_K]           = "K",
    [STAR_M]           = "M",
    [STAR_WHITE_DWARF] = "White Dwarf",
    [STAR_NEUTRON]     = "Neutron Star",
    [STAR_BLACK_HOLE]  = "Black Hole",
};

const char *star_class_name(star_class_t class) {
    if ((int)class >= 0 && (int)class < STAR_CLASS_COUNT)
        return STAR_CLASS_NAMES[(int)class];
    return "Unknown";
}

/* ---- Planet type display names ---- */

static const char *PLANET_TYPE_NAMES[PLANET_TYPE_COUNT] = {
    [PLANET_GAS_GIANT]   = "Gas Giant",
    [PLANET_ICE_GIANT]   = "Ice Giant",
    [PLANET_ROCKY]       = "Rocky",
    [PLANET_SUPER_EARTH] = "Super Earth",
    [PLANET_OCEAN]       = "Ocean",
    [PLANET_LAVA]        = "Lava",
    [PLANET_DESERT]      = "Desert",
    [PLANET_ICE]         = "Ice",
    [PLANET_CARBON]      = "Carbon",
    [PLANET_IRON]        = "Iron",
    [PLANET_ROGUE]       = "Rogue",
};

const char *planet_type_name(planet_type_t type) {
    if ((int)type >= 0 && (int)type < PLANET_TYPE_COUNT)
        return PLANET_TYPE_NAMES[(int)type];
    return "Unknown";
}

/* ---- View state machine ---- */

void view_state_init(view_state_t *vs) {
    memset(vs, 0, sizeof(*vs));
    vs->current_view = VIEW_GALAXY;
    vs->selected_system = uid_null();
    vs->selected_planet = uid_null();
    vs->selected_probe = uid_null();
    vs->history_depth = 0;
}

static void view_push_history(view_state_t *vs) {
    if (vs->history_depth < 8) {
        vs->history[vs->history_depth++] = vs->current_view;
    }
}

void view_state_select_system(view_state_t *vs, probe_uid_t system_id) {
    view_push_history(vs);
    vs->current_view = VIEW_SYSTEM;
    vs->selected_system = system_id;
    vs->selected_planet = uid_null();
}

void view_state_select_planet(view_state_t *vs, probe_uid_t planet_id) {
    /* Stay in system view, just select the planet */
    vs->selected_planet = planet_id;
}

void view_state_select_probe(view_state_t *vs, probe_uid_t probe_id) {
    view_push_history(vs);
    vs->current_view = VIEW_PROBE;
    vs->selected_probe = probe_id;
}

void view_state_back(view_state_t *vs) {
    if (vs->history_depth > 0) {
        vs->current_view = vs->history[--vs->history_depth];
    }
    /* else stay where we are */
}

/* ---- Simulation speed control ---- */

/*
 * Speed steps: fractional ticks-per-frame at 60fps.
 * 1 tick = 1 sim-day. Target: 1 sim-day per 24 real-minutes.
 *
 *   tpf          ticks/sec     real-time per day     label
 *   0.000694     1/1440        24 min/day            "24 min/day"
 *   0.00278      1/360         6 min/day             "6 min/day"
 *   0.0167       1/60          1 min/day             "1 min/day"
 *   0.1          6/60          10 sec/day            "10 sec/day"
 *   1            60/sec        1 sec/day             "1 sec/day"
 *   10           600/sec       10 days/sec           "10 days/sec"
 *   100          6000/sec      100 days/sec          "100 days/sec"
 *   1000         60000/sec     ~3 years/sec          "3 years/sec"
 */
static const double SPEED_STEPS[] = {
    0.000694, 0.00278, 0.0167, 0.1, 1, 10, 100, 1000
};
static const char *SPEED_LABELS[] = {
    "24 min/day", "6 min/day", "1 min/day", "10 sec/day",
    "1 sec/day", "10 days/sec", "100 days/sec", "3 years/sec"
};
#define SPEED_STEP_COUNT 8
#define SPEED_DEFAULT_INDEX 0  /* 24 min/day = 1 day in 3h */

void sim_speed_init(sim_speed_t *s) {
    s->speed_index = SPEED_DEFAULT_INDEX;
    s->ticks_per_frame = SPEED_STEPS[SPEED_DEFAULT_INDEX];
    s->accumulator = 0.0;
    s->paused = false;
}

void sim_speed_init_target(sim_speed_t *s, double sim_years, double real_hours, int fps) {
    double total_ticks = sim_years * 365.25;
    double total_frames = real_hours * 3600.0 * fps;
    double tpf = total_ticks / total_frames;

    /* Find closest speed step */
    int best = 0;
    double best_diff = 1e30;
    for (int i = 0; i < SPEED_STEP_COUNT; i++) {
        double diff = (SPEED_STEPS[i] - tpf);
        if (diff < 0) diff = -diff;
        if (diff < best_diff) { best_diff = diff; best = i; }
    }
    s->speed_index = best;
    s->ticks_per_frame = SPEED_STEPS[best];
    s->accumulator = 0.0;
    s->paused = false;
}

void sim_speed_toggle_pause(sim_speed_t *s) {
    s->paused = !s->paused;
}

void sim_speed_up(sim_speed_t *s) {
    if (s->speed_index < SPEED_STEP_COUNT - 1) {
        s->speed_index++;
        s->ticks_per_frame = SPEED_STEPS[s->speed_index];
        s->accumulator = 0.0;
    }
}

void sim_speed_down(sim_speed_t *s) {
    if (s->speed_index > 0) {
        s->speed_index--;
        s->ticks_per_frame = SPEED_STEPS[s->speed_index];
        s->accumulator = 0.0;
    }
}

int sim_speed_ticks_this_frame(sim_speed_t *s) {
    if (s->paused) return 0;
    s->accumulator += s->ticks_per_frame;
    int whole = (int)s->accumulator;
    s->accumulator -= whole;
    return whole;
}

const char *sim_speed_label(const sim_speed_t *s) {
    return SPEED_LABELS[s->speed_index];
}

/* ---- 2D camera (orthographic) ---- */

void world_to_screen(const camera_2d_t *cam, double wx, double wy,
                     double *sx, double *sy) {
    *sx = wx * cam->scale + cam->offset_x;
    *sy = wy * cam->scale + cam->offset_y;
}

void screen_to_world(const camera_2d_t *cam, double sx, double sy,
                     double *wx, double *wy) {
    *wx = (sx - cam->offset_x) / cam->scale;
    *wy = (sy - cam->offset_y) / cam->scale;
}

void camera_zoom(camera_2d_t *cam, double factor) {
    cam->scale *= factor;
    if (cam->scale < 0.01) cam->scale = 0.01;
    if (cam->scale > 10000.0) cam->scale = 10000.0;
}

/* ---- Hit testing ---- */

probe_uid_t hit_test_system(const system_t *systems, int count,
                      const camera_2d_t *cam,
                      double screen_x, double screen_y,
                      double threshold_px) {
    probe_uid_t best = uid_null();
    double best_dist = threshold_px * threshold_px;

    for (int i = 0; i < count; i++) {
        double sx, sy;
        world_to_screen(cam, systems[i].position.x, systems[i].position.y,
                        &sx, &sy);
        double dx = sx - screen_x;
        double dy = sy - screen_y;
        double d2 = dx * dx + dy * dy;
        if (d2 < best_dist) {
            best_dist = d2;
            best = systems[i].id;
        }
    }
    return best;
}

/* ---- Probe trail (ring buffer) ---- */

void probe_trail_init(probe_trail_t *t) {
    t->head = 0;
    t->count = 0;
}

void probe_trail_push(probe_trail_t *t, vec3_t point) {
    t->points[t->head] = point;
    t->head = (t->head + 1) % TRAIL_MAX_POINTS;
    if (t->count < TRAIL_MAX_POINTS)
        t->count++;
}

vec3_t probe_trail_get(const probe_trail_t *t, int index) {
    if (index < 0 || index >= t->count)
        return (vec3_t){0, 0, 0};
    /* Index 0 = oldest, count-1 = newest.
     * Oldest is at (head - count) mod cap. */
    int start = (t->head - t->count + TRAIL_MAX_POINTS) % TRAIL_MAX_POINTS;
    int actual = (start + index) % TRAIL_MAX_POINTS;
    return t->points[actual];
}

/* ---- Planet orbital position ---- */

void planet_orbital_pos(const planet_t *p, uint64_t tick,
                        double *out_x, double *out_y) {
    if (p->orbital_period_days <= 0) {
        *out_x = p->orbital_radius_au;
        *out_y = 0;
        return;
    }
    /* angle = 2π * (tick / period) */
    double angle = 2.0 * M_PI * ((double)tick / p->orbital_period_days);
    *out_x = p->orbital_radius_au * cos(angle);
    *out_y = p->orbital_radius_au * sin(angle);
}
