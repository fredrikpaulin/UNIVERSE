/*
 * travel.c — Interstellar travel, sensors, and arrival
 *
 * Phase 3 implementation:
 *   - Initiate travel (set heading, speed, fuel reservation)
 *   - Per-tick travel (fuel burn, distance decrement, hazards, arrival)
 *   - Long-range sensor scan (range-limited, sorted by distance)
 *   - Lorentz factor calculation
 */
#include "travel.h"
#include "generate.h"
#include <math.h>
#include <string.h>
#include <stdlib.h>

/* ---- Constants ---- */

#define FUEL_BURN_PER_LY_KG  0.5    /* kg of fuel per ly traveled */
#define MICROMETEORITE_CHANCE 0.0005 /* per tick probability */
#define MICROMETEORITE_DMG    0.005  /* hull damage per hit */
#define MIN_FUEL_FOR_TRAVEL   10.0   /* minimum fuel to initiate */

/* ---- Helpers ---- */

static double vec3_dist(vec3_t a, vec3_t b) {
    double dx = a.x - b.x;
    double dy = a.y - b.y;
    double dz = a.z - b.z;
    return sqrt(dx*dx + dy*dy + dz*dz);
}

/* ---- Lorentz factor ---- */

double travel_lorentz_factor(double speed_c) {
    if (speed_c <= 0.0) return 1.0;
    if (speed_c >= 1.0) return 1e10; /* clamp, can't reach c */
    return 1.0 / sqrt(1.0 - speed_c * speed_c);
}

/* ---- Initiate travel ---- */

travel_result_t travel_initiate(probe_t *probe, const travel_order_t *order) {
    travel_result_t res = {false, 0};

    /* Must not already be traveling */
    if (probe->status == STATUS_TRAVELING) return res;

    /* Calculate distance */
    double dist = vec3_dist(probe->heading, order->target_pos);
    if (dist < 0.001) {
        /* Already there */
        res.success = true;
        res.estimated_ticks = 0;
        return res;
    }

    /* Check fuel — need enough for at least partial journey */
    double fuel_needed = dist * FUEL_BURN_PER_LY_KG;
    if (probe->fuel_kg < MIN_FUEL_FOR_TRAVEL && fuel_needed > probe->fuel_kg) {
        return res; /* insufficient fuel */
    }

    /* Set travel state */
    probe->status = STATUS_TRAVELING;
    probe->location_type = LOC_INTERSTELLAR;
    probe->speed_c = (double)probe->max_speed_c;
    probe->travel_remaining_ly = dist;
    probe->destination = order->target_pos;
    probe->system_id = order->target_system_id;
    probe->sector = order->target_sector;

    /* Compute heading direction (not used for movement math, but stored) */
    /* heading stores current position for distance calculations */

    /* Estimate ticks: distance / speed_c gives years, * TICKS_PER_CYCLE */
    double travel_years = dist / probe->speed_c;
    res.estimated_ticks = (uint64_t)(travel_years * TICKS_PER_CYCLE);
    res.success = true;

    return res;
}

/* ---- Per-tick travel ---- */

travel_tick_result_t travel_tick(probe_t *probe, rng_t *rng) {
    travel_tick_result_t res = {false, false};

    if (probe->status != STATUS_TRAVELING) return res;

    /* Distance covered this tick: speed_c [ly/year] / TICKS_PER_CYCLE [ticks/year] */
    double ly_per_tick = probe->speed_c / (double)TICKS_PER_CYCLE;

    /* Fuel consumption this tick */
    double fuel_cost = ly_per_tick * FUEL_BURN_PER_LY_KG;
    if (probe->fuel_kg < fuel_cost) {
        /* Out of fuel — enter drift */
        probe->fuel_kg = 0;
        probe->status = STATUS_DORMANT;
        probe->speed_c = 0;
        res.fuel_exhausted = true;
        return res;
    }
    probe->fuel_kg -= fuel_cost;

    /* Advance position */
    probe->travel_remaining_ly -= ly_per_tick;

    /* Update heading (interpolate toward destination) */
    if (probe->travel_remaining_ly > 0) {
        double total_dist = vec3_dist(probe->heading, probe->destination);
        if (total_dist > 0.001) {
            double frac = ly_per_tick / total_dist;
            if (frac > 1.0) frac = 1.0;
            probe->heading.x += (probe->destination.x - probe->heading.x) * frac;
            probe->heading.y += (probe->destination.y - probe->heading.y) * frac;
            probe->heading.z += (probe->destination.z - probe->heading.z) * frac;
        }
    }

    /* Micrometeorite hazard */
    double roll = rng_double(rng);
    if (roll < MICROMETEORITE_CHANCE) {
        probe->hull_integrity -= MICROMETEORITE_DMG;
        if (probe->hull_integrity < 0.0f) probe->hull_integrity = 0.0f;
    }

    /* Check arrival */
    if (probe->travel_remaining_ly <= 0.0) {
        probe->travel_remaining_ly = 0.0;
        probe->status = STATUS_ACTIVE;
        probe->location_type = LOC_IN_SYSTEM;
        probe->heading = probe->destination;
        probe->speed_c = 0;
        res.arrived = true;
    }

    return res;
}

/* ---- Long-range scan ---- */

/* qsort comparator for scan results by distance */
static int scan_cmp(const void *a, const void *b) {
    double da = ((const scan_result_t *)a)->distance_ly;
    double db = ((const scan_result_t *)b)->distance_ly;
    if (da < db) return -1;
    if (da > db) return  1;
    return 0;
}

int travel_scan(const probe_t *probe, const system_t *systems, int system_count,
                scan_result_t *out, int max_results) {
    int found = 0;
    double range = (double)probe->sensor_range_ly;

    for (int i = 0; i < system_count && found < max_results; i++) {
        double dist = vec3_dist(probe->heading, systems[i].position);
        /* Skip self (distance ~0) and out-of-range */
        if (dist < 0.001) continue;
        if (dist > range) continue;

        out[found].system_id = systems[i].id;
        out[found].star_class = systems[i].stars[0].class;
        out[found].distance_ly = dist;
        found++;
    }

    /* Sort by distance */
    if (found > 1) {
        qsort(out, (size_t)found, sizeof(scan_result_t), scan_cmp);
    }

    return found;
}
