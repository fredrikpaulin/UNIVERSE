/*
 * render_raylib.h — Raylib rendering integration
 *
 * Only compiled when USE_RAYLIB is defined.
 * Bridges the render logic layer (render.h) to actual Raylib draw calls.
 */
#ifndef RENDER_RAYLIB_H
#define RENDER_RAYLIB_H

#include "universe.h"
#include "render.h"
#include "generate.h"

/* ---- Renderer state ---- */

typedef struct {
    /* View management */
    view_state_t    view;
    sim_speed_t     speed;
    camera_2d_t     galaxy_cam;
    camera_2d_t     system_cam;

    /* Cached sector data */
    system_t        visible_systems[256];
    int             visible_system_count;
    uint64_t        galaxy_seed;

    /* Probe trail */
    probe_trail_t   trail;

    /* Window state */
    int             screen_w;
    int             screen_h;
    bool            show_help;

    /* Info panel */
    int             hovered_planet;  /* -1 = none */
} renderer_t;

/* Initialize window + renderer state */
void renderer_init(renderer_t *r, int width, int height, uint64_t galaxy_seed);

/* Shut down window */
void renderer_close(renderer_t *r);

/* Process input, advance view state. Returns false if window should close. */
bool renderer_update(renderer_t *r, universe_t *u);

/* Draw one frame based on current view */
void renderer_draw(renderer_t *r, const universe_t *u);

/* Load systems for the visible area around probe */
void renderer_load_nearby(renderer_t *r, const probe_t *probe);

#endif
