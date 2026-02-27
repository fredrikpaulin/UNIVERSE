/*
 * travel.h — Interstellar travel, sensors, and arrival
 *
 * API defined by Phase 3 tests. Implementation in travel.c.
 */
#ifndef TRAVEL_H
#define TRAVEL_H

#include "universe.h"
#include "rng.h"

/* ---- Travel order (input to travel_initiate) ---- */

typedef struct {
    vec3_t         target_pos;        /* galactic position in ly */
    probe_uid_t    target_system_id;  /* destination system UID */
    sector_coord_t target_sector;     /* destination sector */
} travel_order_t;

/* ---- Travel initiation result ---- */

typedef struct {
    bool     success;          /* Was travel accepted? */
    uint64_t estimated_ticks;  /* Estimated ticks to arrival */
} travel_result_t;

/* ---- Per-tick travel result ---- */

typedef struct {
    bool arrived;        /* Did we arrive this tick? */
    bool fuel_exhausted; /* Did we run out of fuel? */
} travel_tick_result_t;

/* ---- Scan result (limited info from long-range sensors) ---- */

typedef struct {
    probe_uid_t  system_id;
    star_class_t star_class;
    double       distance_ly;
} scan_result_t;

/* ---- API ---- */

/* Begin interstellar travel toward target. Sets probe status to TRAVELING. */
travel_result_t travel_initiate(probe_t *probe, const travel_order_t *order);

/* Advance one tick of travel. Consumes fuel, applies hazards, checks arrival. */
travel_tick_result_t travel_tick(probe_t *probe, rng_t *rng);

/* Long-range scan: find systems within sensor_range_ly.
 * Returns number of results written (sorted by distance). */
int travel_scan(const probe_t *probe, const system_t *systems, int system_count,
                scan_result_t *out, int max_results);

/* Lorentz factor: gamma = 1 / sqrt(1 - v^2/c^2) */
double travel_lorentz_factor(double speed_c);

#endif
