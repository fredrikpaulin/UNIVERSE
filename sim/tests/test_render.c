#define _POSIX_C_SOURCE 199309L
/*
 * test_render.c — Phase 5 tests: render logic layer
 *
 * Written BEFORE implementation. Defines the render.h API contract.
 *
 * Since we can't link Raylib in CI, these tests exercise the LOGIC layer
 * that sits underneath the actual Raylib draw calls:
 *   - Star spectral class → color mapping
 *   - View state machine (galaxy → system → probe dashboard)
 *   - Simulation speed control (pause, 1x, 10x, 100x, max)
 *   - Camera projection helpers (world ↔ screen coords)
 *   - Hit testing (screen click → nearest star/planet)
 *   - Probe trail (path history buffer)
 *   - Planet orbital position at a given tick
 *   - Layout geometry (panel sizes, margins)
 */
#include "universe.h"
#include "rng.h"
#include "generate.h"
#include "probe.h"
#include "render.h"
#include "util.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <math.h>

static int tests_passed = 0;
static int tests_failed = 0;

#define ASSERT(cond, msg) do { \
    if (!(cond)) { \
        fprintf(stderr, "  FAIL: %s (line %d)\n", msg, __LINE__); \
        tests_failed++; \
    } else { \
        tests_passed++; \
    } \
} while(0)

#define ASSERT_NEAR(a, b, tol, msg) ASSERT(fabs((double)(a)-(double)(b)) < (tol), msg)

/* ---- Test: Star spectral class → RGB color ---- */
static void test_star_colors(void) {
    printf("Test: Star spectral class → color\n");

    /* Each class should map to a distinct, non-zero color */
    for (int i = 0; i < STAR_CLASS_COUNT; i++) {
        rgba_t c = star_class_color((star_class_t)i);
        /* At least one channel should be non-zero (visible) */
        ASSERT(c.r > 0 || c.g > 0 || c.b > 0, "Color is visible");
        ASSERT(c.a > 0, "Alpha is non-zero");
    }

    /* O stars should be blue-ish (b > r) */
    rgba_t o = star_class_color(STAR_O);
    ASSERT(o.b >= o.r, "O stars are blue-ish");

    /* M stars should be red-ish (r > b) */
    rgba_t m = star_class_color(STAR_M);
    ASSERT(m.r > m.b, "M stars are red-ish");

    /* G stars (sun-like) should be yellow-ish (r and g high) */
    rgba_t g = star_class_color(STAR_G);
    ASSERT(g.r > 128 && g.g > 128, "G stars are yellow-ish");

    /* White dwarf should be white-ish (all channels similar) */
    rgba_t wd = star_class_color(STAR_WHITE_DWARF);
    ASSERT(abs((int)wd.r - (int)wd.b) < 60, "White dwarf is white-ish");

    /* Black hole should be dark */
    rgba_t bh = star_class_color(STAR_BLACK_HOLE);
    ASSERT(bh.r < 80 && bh.g < 80 && bh.b < 80, "Black hole is dark");
}

/* ---- Test: View state machine ---- */
static void test_view_state(void) {
    printf("Test: View state machine\n");

    view_state_t vs;
    view_state_init(&vs);

    ASSERT(vs.current_view == VIEW_GALAXY, "Starts in galaxy view");
    ASSERT(uid_is_null(vs.selected_system), "No system selected");
    ASSERT(uid_is_null(vs.selected_planet), "No planet selected");
    ASSERT(uid_is_null(vs.selected_probe), "No probe selected");

    /* Drill down: galaxy → system */
    probe_uid_t sys_id = {42, 1};
    view_state_select_system(&vs, sys_id);
    ASSERT(vs.current_view == VIEW_SYSTEM, "Switched to system view");
    ASSERT(uid_eq(vs.selected_system, sys_id), "System ID stored");

    /* Drill down: system → planet (stays in system view but selects planet) */
    probe_uid_t pl_id = {42, 100};
    view_state_select_planet(&vs, pl_id);
    ASSERT(vs.current_view == VIEW_SYSTEM, "Still in system view");
    ASSERT(uid_eq(vs.selected_planet, pl_id), "Planet ID stored");

    /* Switch to probe dashboard */
    probe_uid_t pr_id = {1, 1};
    view_state_select_probe(&vs, pr_id);
    ASSERT(vs.current_view == VIEW_PROBE, "Switched to probe view");
    ASSERT(uid_eq(vs.selected_probe, pr_id), "Probe ID stored");

    /* Back navigation: probe → system → galaxy */
    view_state_back(&vs);
    ASSERT(vs.current_view == VIEW_SYSTEM, "Back to system view");

    view_state_back(&vs);
    ASSERT(vs.current_view == VIEW_GALAXY, "Back to galaxy view");

    /* Back from galaxy stays in galaxy */
    view_state_back(&vs);
    ASSERT(vs.current_view == VIEW_GALAXY, "Can't go back past galaxy");
}

/* ---- Test: Speed control ---- */
static void test_speed_control(void) {
    printf("Test: Speed control\n");

    sim_speed_t spd;
    sim_speed_init(&spd);

    /* Default is slowest step: ~0.000694 tpf (24 min/day) */
    ASSERT(spd.ticks_per_frame < 0.001, "Default is slowest (24 min/day)");
    ASSERT(spd.speed_index == 0, "Default index 0");
    ASSERT(!spd.paused, "Not paused by default");

    /* Can't go below min */
    sim_speed_down(&spd);
    ASSERT(spd.speed_index == 0, "Min capped at 0");

    /* Pause */
    sim_speed_toggle_pause(&spd);
    ASSERT(spd.paused, "Now paused");
    ASSERT(sim_speed_ticks_this_frame(&spd) == 0, "Paused → 0 ticks");

    /* Unpause */
    sim_speed_toggle_pause(&spd);
    ASSERT(!spd.paused, "Unpaused");

    /* Speed up to 1 sec/day (index 4, tpf=1) */
    sim_speed_up(&spd); /* index 1 */
    sim_speed_up(&spd); /* index 2 */
    sim_speed_up(&spd); /* index 3 */
    sim_speed_up(&spd); /* index 4: tpf=1 */
    ASSERT(spd.ticks_per_frame >= 0.99 && spd.ticks_per_frame <= 1.01,
           "1 sec/day step");
    ASSERT(sim_speed_ticks_this_frame(&spd) == 1, "1 tick/frame");

    /* Keep going up to max */
    sim_speed_up(&spd); /* 10 */
    ASSERT(spd.ticks_per_frame >= 9.9, "10 days/sec");
    sim_speed_up(&spd); /* 100 */
    sim_speed_up(&spd); /* 1000 */
    ASSERT(spd.ticks_per_frame >= 999.9, "Max step 1000 tpf");

    double max_tpf = spd.ticks_per_frame;
    sim_speed_up(&spd);
    ASSERT(spd.ticks_per_frame == max_tpf, "Max capped");

    /* Speed down back to 1 sec/day */
    sim_speed_down(&spd); /* 100 */
    sim_speed_down(&spd); /* 10 */
    sim_speed_down(&spd); /* 1 */
    ASSERT(spd.ticks_per_frame >= 0.99 && spd.ticks_per_frame <= 1.01,
           "Back to 1 sec/day");

    /* Fractional accumulator: at default slow speed, many frames per tick */
    sim_speed_init(&spd);
    int total_ticks = 0;
    /* At 0.000694 tpf, need ~1440 frames for 1 tick */
    for (int i = 0; i < 2000; i++) {
        total_ticks += sim_speed_ticks_this_frame(&spd);
    }
    ASSERT(total_ticks >= 1 && total_ticks <= 2,
           "~1 tick in 2000 frames at 24min/day speed");

    /* Target init: 1 day in 24 min at 60fps → picks index 0 */
    sim_speed_init_target(&spd, 1.0 / 365.25, 0.4, 60);
    ASSERT(spd.ticks_per_frame < 0.001, "Target matches slowest step");
    ASSERT(!spd.paused, "Target init not paused");
    ASSERT(sim_speed_label(&spd) != NULL, "Has label");
}

/* ---- Test: Planet orbital position at tick ---- */
static void test_orbital_position(void) {
    printf("Test: Planet orbital position at tick\n");

    planet_t p;
    memset(&p, 0, sizeof(p));
    p.orbital_radius_au = 1.0;
    p.orbital_period_days = 365.25;

    /* At tick 0, planet is at angle 0 → position (radius, 0) */
    double x, y;
    planet_orbital_pos(&p, 0, &x, &y);
    ASSERT_NEAR(x, 1.0, 0.01, "At tick 0, x ≈ radius");
    ASSERT_NEAR(y, 0.0, 0.01, "At tick 0, y ≈ 0");

    /* At tick = period/4, planet is at 90° → (0, radius) */
    /* ticks = days for orbital period (1 tick = 1 day in the sim) */
    uint64_t quarter = (uint64_t)(p.orbital_period_days / 4.0);
    planet_orbital_pos(&p, quarter, &x, &y);
    ASSERT_NEAR(x, 0.0, 0.05, "At quarter period, x ≈ 0");
    ASSERT_NEAR(y, 1.0, 0.05, "At quarter period, y ≈ radius");

    /* At tick = period/2, planet is at 180° → (-radius, 0) */
    uint64_t half = (uint64_t)(p.orbital_period_days / 2.0);
    planet_orbital_pos(&p, half, &x, &y);
    ASSERT_NEAR(x, -1.0, 0.05, "At half period, x ≈ -radius");
    ASSERT_NEAR(y, 0.0, 0.05, "At half period, y ≈ 0");

    /* Full orbit brings it back */
    uint64_t full = (uint64_t)p.orbital_period_days;
    planet_orbital_pos(&p, full, &x, &y);
    ASSERT_NEAR(x, 1.0, 0.05, "At full period, x ≈ radius (back)");
    ASSERT_NEAR(y, 0.0, 0.05, "At full period, y ≈ 0 (back)");
}

/* ---- Test: Hit testing — find nearest system to screen coords ---- */
static void test_hit_test_system(void) {
    printf("Test: Hit test — find nearest system\n");

    /* Set up a few systems at known positions */
    system_t systems[5];
    memset(systems, 0, sizeof(systems));

    systems[0].position = (vec3_t){0.0, 0.0, 0.0};
    systems[0].id = (probe_uid_t){1, 1};
    systems[1].position = (vec3_t){10.0, 0.0, 0.0};
    systems[1].id = (probe_uid_t){2, 2};
    systems[2].position = (vec3_t){0.0, 10.0, 0.0};
    systems[2].id = (probe_uid_t){3, 3};
    systems[3].position = (vec3_t){5.0, 5.0, 0.0};
    systems[3].id = (probe_uid_t){4, 4};
    systems[4].position = (vec3_t){100.0, 100.0, 0.0};
    systems[4].id = (probe_uid_t){5, 5};

    /* Simple orthographic projection: screen = world * scale + offset
     * Using scale=10, offset=(400, 300) → center of 800×600 screen */
    camera_2d_t cam = {
        .offset_x = 400.0, .offset_y = 300.0,
        .scale = 10.0
    };

    /* Click near system 0's screen position (400, 300) */
    probe_uid_t hit = hit_test_system(systems, 5, &cam, 402.0, 298.0, 20.0);
    ASSERT(uid_eq(hit, systems[0].id), "Hit system at origin");

    /* Click near system 1 at screen (500, 300) → (10*10+400, 0*10+300) */
    hit = hit_test_system(systems, 5, &cam, 498.0, 302.0, 20.0);
    ASSERT(uid_eq(hit, systems[1].id), "Hit system at (10,0)");

    /* Click near system 3 at screen (450, 350) → (5*10+400, 5*10+300) */
    hit = hit_test_system(systems, 5, &cam, 452.0, 348.0, 20.0);
    ASSERT(uid_eq(hit, systems[3].id), "Hit system at (5,5)");

    /* Click far from any system → null */
    hit = hit_test_system(systems, 5, &cam, 10.0, 10.0, 20.0);
    ASSERT(uid_is_null(hit), "Miss returns null UID");
}

/* ---- Test: Probe trail buffer ---- */
static void test_probe_trail(void) {
    printf("Test: Probe trail (path history)\n");

    probe_trail_t trail;
    probe_trail_init(&trail);

    ASSERT(trail.count == 0, "Starts empty");

    /* Add some points */
    probe_trail_push(&trail, (vec3_t){0, 0, 0});
    probe_trail_push(&trail, (vec3_t){1, 0, 0});
    probe_trail_push(&trail, (vec3_t){2, 0, 0});
    ASSERT(trail.count == 3, "3 points added");

    /* Check retrieval */
    vec3_t p;
    p = probe_trail_get(&trail, 0);
    ASSERT_NEAR(p.x, 0.0, 0.01, "First point x=0");
    p = probe_trail_get(&trail, 2);
    ASSERT_NEAR(p.x, 2.0, 0.01, "Third point x=2");

    /* Fill to capacity and verify it wraps (ring buffer) */
    for (int i = 3; i < TRAIL_MAX_POINTS + 10; i++) {
        probe_trail_push(&trail, (vec3_t){(double)i, 0, 0});
    }
    ASSERT(trail.count == TRAIL_MAX_POINTS, "Capped at max");

    /* Most recent point should be the last pushed */
    p = probe_trail_get(&trail, trail.count - 1);
    ASSERT_NEAR(p.x, (double)(TRAIL_MAX_POINTS + 10 - 1), 0.01,
                "Last point is most recent");
}

/* ---- Test: Planet type → display name ---- */
static void test_planet_type_names(void) {
    printf("Test: Planet type display names\n");

    for (int i = 0; i < PLANET_TYPE_COUNT; i++) {
        const char *name = planet_type_name((planet_type_t)i);
        ASSERT(name != NULL, "Name is not NULL");
        ASSERT(strlen(name) > 0, "Name is not empty");
    }

    /* Specific checks */
    ASSERT(strcmp(planet_type_name(PLANET_GAS_GIANT), "Gas Giant") == 0,
           "Gas giant name");
    ASSERT(strcmp(planet_type_name(PLANET_ROCKY), "Rocky") == 0,
           "Rocky name");
    ASSERT(strcmp(planet_type_name(PLANET_OCEAN), "Ocean") == 0,
           "Ocean name");
}

/* ---- Test: Star class → display name ---- */
static void test_star_class_names(void) {
    printf("Test: Star class display names\n");

    for (int i = 0; i < STAR_CLASS_COUNT; i++) {
        const char *name = star_class_name((star_class_t)i);
        ASSERT(name != NULL, "Name is not NULL");
        ASSERT(strlen(name) > 0, "Name is not empty");
    }

    ASSERT(strcmp(star_class_name(STAR_G), "G") == 0, "G class");
    ASSERT(strcmp(star_class_name(STAR_BLACK_HOLE), "Black Hole") == 0,
           "Black hole name");
}

/* ---- Test: Camera world ↔ screen conversion round-trip ---- */
static void test_camera_conversion(void) {
    printf("Test: Camera world ↔ screen round-trip\n");

    camera_2d_t cam = {
        .offset_x = 400.0, .offset_y = 300.0,
        .scale = 10.0
    };

    /* World (5, 3) → screen (450, 330) */
    double sx, sy;
    world_to_screen(&cam, 5.0, 3.0, &sx, &sy);
    ASSERT_NEAR(sx, 450.0, 0.01, "World→screen x");
    ASSERT_NEAR(sy, 330.0, 0.01, "World→screen y");

    /* Screen (450, 330) → world (5, 3) */
    double wx, wy;
    screen_to_world(&cam, 450.0, 330.0, &wx, &wy);
    ASSERT_NEAR(wx, 5.0, 0.01, "Screen→world x");
    ASSERT_NEAR(wy, 3.0, 0.01, "Screen→world y");

    /* Round-trip at origin */
    world_to_screen(&cam, 0.0, 0.0, &sx, &sy);
    ASSERT_NEAR(sx, 400.0, 0.01, "Origin screen x");
    ASSERT_NEAR(sy, 300.0, 0.01, "Origin screen y");
    screen_to_world(&cam, sx, sy, &wx, &wy);
    ASSERT_NEAR(wx, 0.0, 0.01, "Round-trip origin x");
    ASSERT_NEAR(wy, 0.0, 0.01, "Round-trip origin y");
}

/* ---- Test: Zoom changes scale ---- */
static void test_camera_zoom(void) {
    printf("Test: Camera zoom\n");

    camera_2d_t cam = {
        .offset_x = 400.0, .offset_y = 300.0,
        .scale = 10.0
    };

    camera_zoom(&cam, 1.5);
    ASSERT_NEAR(cam.scale, 15.0, 0.01, "Zoomed in to 15x");

    camera_zoom(&cam, 0.5);
    ASSERT_NEAR(cam.scale, 7.5, 0.01, "Zoomed out to 7.5x");

    /* Zoom should clamp to reasonable range */
    camera_zoom(&cam, 10000.0);
    ASSERT(cam.scale <= 10000.0, "Zoom has upper bound");
    ASSERT(cam.scale >= 0.01, "Zoom has lower bound");
}

/* ---- Main ---- */
int main(void) {
    printf("=== Phase 5: Render Logic Tests ===\n\n");

    test_star_colors();
    printf("\n");
    test_view_state();
    printf("\n");
    test_speed_control();
    printf("\n");
    test_orbital_position();
    printf("\n");
    test_hit_test_system();
    printf("\n");
    test_probe_trail();
    printf("\n");
    test_planet_type_names();
    printf("\n");
    test_star_class_names();
    printf("\n");
    test_camera_conversion();
    printf("\n");
    test_camera_zoom();

    printf("\n=== Results: %d passed, %d failed ===\n",
        tests_passed, tests_failed);
    return tests_failed > 0 ? 1 : 0;
}
