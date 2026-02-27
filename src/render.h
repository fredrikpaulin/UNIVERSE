/*
 * render.h — Render logic layer (no Raylib dependency)
 *
 * This header defines the data structures and pure functions that feed
 * into the actual Raylib draw calls. Testable without a GPU or display.
 *
 * The actual Raylib rendering (render_raylib.c) is only compiled when
 * USE_RAYLIB is defined at build time.
 */
#ifndef RENDER_H
#define RENDER_H

#include "universe.h"

/* ---- Color type (Raylib-compatible RGBA) ---- */

typedef struct {
    uint8_t r, g, b, a;
} rgba_t;

/* ---- Star/planet display helpers ---- */

rgba_t      star_class_color(star_class_t class);
const char *star_class_name(star_class_t class);
const char *planet_type_name(planet_type_t type);

/* ---- View state machine ---- */

typedef enum {
    VIEW_GALAXY,
    VIEW_SYSTEM,
    VIEW_PROBE,
    VIEW_COUNT
} view_t;

typedef struct {
    view_t current_view;
    probe_uid_t  selected_system;
    probe_uid_t  selected_planet;
    probe_uid_t  selected_probe;
    view_t history[8]; /* breadcrumb stack */
    int    history_depth;
} view_state_t;

void view_state_init(view_state_t *vs);
void view_state_select_system(view_state_t *vs, probe_uid_t system_id);
void view_state_select_planet(view_state_t *vs, probe_uid_t planet_id);
void view_state_select_probe(view_state_t *vs, probe_uid_t probe_id);
void view_state_back(view_state_t *vs);

/* ---- Simulation speed control ---- */

typedef struct {
    double ticks_per_frame;   /* can be fractional (e.g. 0.05 = 1 tick every 20 frames) */
    double accumulator;       /* fractional tick accumulator */
    int    speed_index;
    bool   paused;
} sim_speed_t;

void sim_speed_init(sim_speed_t *s);
void sim_speed_init_target(sim_speed_t *s, double sim_years, double real_hours, int fps);
void sim_speed_toggle_pause(sim_speed_t *s);
void sim_speed_up(sim_speed_t *s);
void sim_speed_down(sim_speed_t *s);
int  sim_speed_ticks_this_frame(sim_speed_t *s);  /* advances accumulator */
const char *sim_speed_label(const sim_speed_t *s);

/* ---- 2D camera (orthographic projection for galaxy/system views) ---- */

typedef struct {
    double offset_x, offset_y; /* screen position of world origin */
    double scale;              /* pixels per world unit (ly or AU) */
} camera_2d_t;

void world_to_screen(const camera_2d_t *cam, double wx, double wy,
                     double *sx, double *sy);
void screen_to_world(const camera_2d_t *cam, double sx, double sy,
                     double *wx, double *wy);
void camera_zoom(camera_2d_t *cam, double factor);

/* ---- Hit testing ---- */

probe_uid_t hit_test_system(const system_t *systems, int count,
                      const camera_2d_t *cam,
                      double screen_x, double screen_y,
                      double threshold_px);

/* ---- Probe trail (path history ring buffer) ---- */

#define TRAIL_MAX_POINTS 1024

typedef struct {
    vec3_t points[TRAIL_MAX_POINTS];
    int    head;  /* write position */
    int    count; /* number of valid points */
} probe_trail_t;

void  probe_trail_init(probe_trail_t *t);
void  probe_trail_push(probe_trail_t *t, vec3_t point);
vec3_t probe_trail_get(const probe_trail_t *t, int index);

/* ---- Planet orbital position ---- */

void planet_orbital_pos(const planet_t *p, uint64_t tick,
                        double *out_x, double *out_y);

#endif
