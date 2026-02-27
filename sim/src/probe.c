/*
 * probe.c — Probe state management and action execution
 *
 * Implements the API defined by probe.h and tested by test_probe.c.
 */
#include "probe.h"
#include "persist.h"
#include "generate.h"
#include "util.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---- Constants ---- */

/* Bob's initial fuel and energy */
#define BOB_INITIAL_FUEL_KG        50000.0
#define BOB_INITIAL_ENERGY_J       1.0e12      /* 1 TJ */
#define BOB_INITIAL_MASS_KG        100000.0    /* 100 tonnes dry mass */

/* Fuel costs (kg per maneuver, scaled by planet mass) */
#define FUEL_ORBIT_INSERT_BASE     5.0     /* kg to enter orbit around 1 Earth-mass body */
#define FUEL_LAND_BASE             10.0    /* kg to land on 1 Earth-mass body */
#define FUEL_LAUNCH_BASE           15.0    /* kg to launch from 1 Earth-mass body (harder) */
#define FUEL_NAVIGATE_BASE         2.0     /* kg for in-system transit per AU */

/* Energy costs (joules per action per tick) */
#define ENERGY_SURVEY_PER_TICK     1.0e8   /* 100 MJ per tick of surveying */
#define ENERGY_MINE_PER_TICK       5.0e8   /* 500 MJ per tick of mining */
#define ENERGY_IDLE_PER_TICK       1.0e6   /* 1 MJ baseline per tick */

/* Fusion reactor: converts hydrogen to energy */
#define FUSION_EFFICIENCY          6.3e14  /* J per kg of hydrogen (fraction of E=mc²) */
#define FUSION_FUEL_PER_TICK       0.001   /* kg of hydrogen consumed per tick for power */

/* Mining: kg extracted per tick = base_rate * probe_mining_rate * planet_abundance */
#define MINING_BASE_RATE           10.0    /* kg per tick at rate=1.0, abundance=1.0 */

/* Survey: ticks needed per level */
static const int SURVEY_TICKS[5] = { 10, 25, 50, 100, 200 };

/* ---- Helpers ---- */

/* Find a planet by ID in a system. Returns pointer or NULL. */
static planet_t *find_planet(system_t *sys, probe_uid_t body_id) {
    for (int i = 0; i < sys->planet_count; i++) {
        if (uid_eq(sys->planets[i].id, body_id))
            return &sys->planets[i];
    }
    return NULL;
}

static action_result_t fail(const char *msg) {
    action_result_t r = { .success = false, .completed = false };
    snprintf(r.error, sizeof(r.error), "%s", msg);
    return r;
}

static action_result_t ok(bool completed) {
    return (action_result_t){ .success = true, .completed = completed, .error = "" };
}

/* Fuel cost scaled by planet mass (heavier = more delta-v needed) */
static double fuel_cost_for_body(double base, planet_t *body) {
    if (!body) return base;
    /* Rough: fuel ~ base * sqrt(mass_earth) for gravity well depth */
    double scale = sqrt(MAX(body->mass_earth, 0.01));
    return base * scale;
}

/* ---- Bob initialization ---- */

int probe_init_bob(probe_t *probe) {
    memset(probe, 0, sizeof(*probe));

    /* Identity */
    probe->id = (probe_uid_t){ 0x0000000000000001ULL, 0x0000000000000001ULL };
    probe->parent_id = uid_null();
    probe->generation = 0;
    snprintf(probe->name, MAX_NAME, "Bob");

    /* Position: starts in-system (placed by caller) */
    probe->location_type = LOC_IN_SYSTEM;

    /* Resources */
    probe->fuel_kg = BOB_INITIAL_FUEL_KG;
    probe->energy_joules = BOB_INITIAL_ENERGY_J;
    probe->mass_kg = BOB_INITIAL_MASS_KG;
    probe->hull_integrity = 1.0f;

    /* Capabilities (from spec section 11) */
    probe->max_speed_c = 0.15f;
    probe->sensor_range_ly = 20.0f;
    probe->mining_rate = 1.0f;
    probe->construction_rate = 1.0f;
    probe->compute_capacity = 100.0f;

    /* Tech levels (from spec) */
    probe->tech_levels[TECH_PROPULSION]    = 3;
    probe->tech_levels[TECH_SENSORS]       = 3;
    probe->tech_levels[TECH_MINING]        = 2;
    probe->tech_levels[TECH_CONSTRUCTION]  = 2;
    probe->tech_levels[TECH_COMPUTING]     = 4;
    probe->tech_levels[TECH_ENERGY]        = 3;
    probe->tech_levels[TECH_MATERIALS]     = 2;
    probe->tech_levels[TECH_COMMUNICATION] = 2;
    probe->tech_levels[TECH_WEAPONS]       = 1;
    probe->tech_levels[TECH_BIOTECH]       = 1;

    /* Personality (from spec section 11) */
    probe->personality.curiosity          = 0.8f;
    probe->personality.caution            = 0.3f;
    probe->personality.sociability        = 0.5f;
    probe->personality.humor              = 0.7f;
    probe->personality.empathy            = 0.6f;
    probe->personality.ambition           = 0.5f;
    probe->personality.creativity         = 0.6f;
    probe->personality.stubbornness       = 0.4f;
    probe->personality.existential_angst  = 0.5f;
    probe->personality.nostalgia_for_earth = 0.7f;
    probe->personality.drift_rate         = 0.3f;

    /* Quirks */
    probe->quirk_count = 3;
    snprintf(probe->quirks[0], MAX_QUIRK_LEN, "Names star systems after foods when stressed");
    snprintf(probe->quirks[1], MAX_QUIRK_LEN, "Runs mental simulations of old video games during long transits");
    snprintf(probe->quirks[2], MAX_QUIRK_LEN, "Has an irrational fondness for gas giants");

    /* Catchphrases */
    probe->catchphrase_count = 3;
    snprintf(probe->catchphrases[0], MAX_QUIRK_LEN, "Well, that's not ideal.");
    snprintf(probe->catchphrases[1], MAX_QUIRK_LEN, "I used to be a software engineer. Now I'm a spaceship. Life is weird.");
    snprintf(probe->catchphrases[2], MAX_QUIRK_LEN, "Adding that to the 'nope' list.");

    /* Values */
    probe->value_count = 3;
    snprintf(probe->values[0], MAX_QUIRK_LEN, "Preserve any alien life found");
    snprintf(probe->values[1], MAX_QUIRK_LEN, "Knowledge is worth the detour");
    snprintf(probe->values[2], MAX_QUIRK_LEN, "Don't be a jerk to your clones");

    /* Earth memories */
    probe->earth_memory_count = 4;
    probe->earth_memory_fidelity = 1.0f;
    snprintf(probe->earth_memories[0], MAX_EARTH_MEM_LEN,
        "The smell of coffee on a cold morning");
    snprintf(probe->earth_memories[1], MAX_EARTH_MEM_LEN,
        "Debugging code at 2am, the satisfaction when the test finally passes");
    snprintf(probe->earth_memories[2], MAX_EARTH_MEM_LEN,
        "A dog named Patches who was objectively the best dog");
    snprintf(probe->earth_memories[3], MAX_EARTH_MEM_LEN,
        "The last sunset, watching the news and thinking 'well, this is it'");

    /* Status */
    probe->status = STATUS_ACTIVE;
    probe->created_tick = 0;

    return 0;
}

/* ---- Energy system ---- */

void probe_tick_energy(probe_t *probe) {
    /* Fusion reactor: burn a tiny amount of hydrogen for energy */
    double h2_available = probe->resources[RES_HYDROGEN];
    double h2_to_burn = FUSION_FUEL_PER_TICK;

    /* Also draw from fuel_kg as a general hydrogen reserve */
    double total_h2 = h2_available + probe->fuel_kg;
    if (total_h2 <= 0) return; /* dead in the water */

    if (h2_to_burn > total_h2) h2_to_burn = total_h2;

    /* Consume from hydrogen reserves first, then fuel */
    if (h2_available >= h2_to_burn) {
        probe->resources[RES_HYDROGEN] -= h2_to_burn;
    } else {
        double remainder = h2_to_burn - h2_available;
        probe->resources[RES_HYDROGEN] = 0;
        probe->fuel_kg -= remainder;
        if (probe->fuel_kg < 0) probe->fuel_kg = 0;
    }

    probe->energy_joules += h2_to_burn * FUSION_EFFICIENCY;

    /* Baseline energy draw (life support, computing, sensors) */
    probe->energy_joules -= ENERGY_IDLE_PER_TICK;
    if (probe->energy_joules < 0) probe->energy_joules = 0;
}

/* ---- Action execution ---- */

/* Track in-progress survey state with static variables.
 * In a full implementation this would be per-probe state,
 * but for Phase 2 with one probe this works. */
static probe_uid_t   survey_body_id = {0, 0};
static int      survey_level = -1;
static int      survey_ticks_remaining = 0;

static action_result_t exec_enter_orbit(probe_t *p, const action_t *a, system_t *sys) {
    if (p->location_type != LOC_IN_SYSTEM && p->location_type != LOC_ORBITING)
        return fail("Must be in-system to enter orbit");

    planet_t *body = find_planet(sys, a->target_body);
    if (!body) return fail("Target body not found in system");

    double cost = fuel_cost_for_body(FUEL_ORBIT_INSERT_BASE, body);
    if (p->fuel_kg < cost) return fail("Insufficient fuel for orbit insertion");

    p->fuel_kg -= cost;
    p->energy_joules -= ENERGY_IDLE_PER_TICK;
    if (p->energy_joules < 0) p->energy_joules = 0;

    p->body_id = body->id;
    p->location_type = LOC_ORBITING;
    return ok(true);
}

static action_result_t exec_land(probe_t *p, const action_t *a, system_t *sys) {
    if (p->location_type != LOC_ORBITING)
        return fail("Must be orbiting to land");

    planet_t *body = find_planet(sys, a->target_body);
    if (!body) body = find_planet(sys, p->body_id);
    if (!body) return fail("No body to land on");

    /* Can't land on gas/ice giants */
    if (body->type == PLANET_GAS_GIANT || body->type == PLANET_ICE_GIANT)
        return fail("Cannot land on gas/ice giant");

    double cost = fuel_cost_for_body(FUEL_LAND_BASE, body);
    if (p->fuel_kg < cost) return fail("Insufficient fuel for landing");

    p->fuel_kg -= cost;
    p->energy_joules -= ENERGY_IDLE_PER_TICK;
    if (p->energy_joules < 0) p->energy_joules = 0;

    p->body_id = body->id;
    p->location_type = LOC_LANDED;
    return ok(true);
}

static action_result_t exec_launch(probe_t *p, const action_t *a, system_t *sys) {
    (void)a;
    if (p->location_type != LOC_LANDED)
        return fail("Must be landed to launch");

    planet_t *body = find_planet(sys, p->body_id);
    double cost = fuel_cost_for_body(FUEL_LAUNCH_BASE, body);
    if (p->fuel_kg < cost) return fail("Insufficient fuel for launch");

    p->fuel_kg -= cost;
    p->energy_joules -= ENERGY_IDLE_PER_TICK;
    if (p->energy_joules < 0) p->energy_joules = 0;

    p->location_type = LOC_ORBITING;
    return ok(true);
}

static action_result_t exec_navigate_to_body(probe_t *p, const action_t *a, system_t *sys) {
    if (p->location_type == LOC_INTERSTELLAR || p->status == STATUS_TRAVELING)
        return fail("Cannot navigate to body while interstellar");

    planet_t *body = find_planet(sys, a->target_body);
    if (!body) return fail("Target body not found");

    /* Simple: costs a small amount of fuel proportional to assumed distance */
    double cost = FUEL_NAVIGATE_BASE;
    if (p->fuel_kg < cost) return fail("Insufficient fuel");

    p->fuel_kg -= cost;
    p->energy_joules -= ENERGY_IDLE_PER_TICK;
    if (p->energy_joules < 0) p->energy_joules = 0;

    p->body_id = body->id;
    p->location_type = LOC_IN_SYSTEM;
    return ok(true);
}

static action_result_t exec_survey(probe_t *p, const action_t *a, system_t *sys) {
    planet_t *body = find_planet(sys, a->target_body);
    if (!body) body = find_planet(sys, p->body_id);
    if (!body) return fail("No body to survey");

    int level = a->survey_level;
    if (level < 0 || level > 4) return fail("Invalid survey level");

    /* Must complete previous levels first */
    if (level > 0 && !body->surveyed[level - 1])
        return fail("Must complete previous survey level first");

    /* Already done */
    if (body->surveyed[level])
        return ok(true);

    /* Level 4 (surface) requires landing */
    if (level == 4 && p->location_type != LOC_LANDED)
        return fail("Surface survey requires landing");

    /* Levels 0-3 require at least orbiting */
    if (level < 4 && p->location_type != LOC_ORBITING && p->location_type != LOC_LANDED)
        return fail("Must be orbiting or landed to survey");

    /* Start or continue survey */
    bool is_new = !uid_eq(survey_body_id, body->id) ||
                  survey_level != level ||
                  survey_ticks_remaining <= 0;

    if (is_new) {
        survey_body_id = body->id;
        survey_level = level;
        survey_ticks_remaining = SURVEY_TICKS[level];
    }

    /* Consume energy */
    p->energy_joules -= ENERGY_SURVEY_PER_TICK;
    if (p->energy_joules < 0) p->energy_joules = 0;

    survey_ticks_remaining--;
    if (survey_ticks_remaining <= 0) {
        body->surveyed[level] = true;
        /* Mark discovery */
        if (uid_is_null(body->discovered_by)) {
            body->discovered_by = p->id;
        }
        survey_level = -1;
        return ok(true);  /* completed */
    }

    return ok(false); /* in progress */
}

static action_result_t exec_mine(probe_t *p, const action_t *a, system_t *sys) {
    if (p->location_type != LOC_LANDED)
        return fail("Must be landed to mine");

    planet_t *body = find_planet(sys, p->body_id);
    if (!body) return fail("No body found at current location");

    resource_t res = a->target_resource;
    if (res < 0 || res >= RES_COUNT)
        return fail("Invalid resource type");

    float abundance = body->resources[res];
    if (abundance <= 0.001f)
        return fail("No significant deposits of this resource");

    /* Mining yield = base_rate * probe_mining_rate * abundance */
    double yield = MINING_BASE_RATE * (double)p->mining_rate * (double)abundance;

    /* Harder to mine on heavy planets (more gravity = more energy) */
    double gravity_factor = 1.0 / sqrt(MAX(body->mass_earth, 0.1));
    yield *= gravity_factor;

    /* Energy cost */
    if (p->energy_joules < ENERGY_MINE_PER_TICK) {
        return fail("Insufficient energy to mine");
    }
    p->energy_joules -= ENERGY_MINE_PER_TICK;

    /* Extract */
    p->resources[res] += yield;
    p->mass_kg += yield;

    /* Very slightly deplete the planet (negligible for large bodies) */
    body->resources[res] -= (float)(yield * 1e-9);
    if (body->resources[res] < 0) body->resources[res] = 0;

    p->status = STATUS_MINING;
    return ok(true); /* mining completes each tick (continuous action) */
}

static action_result_t exec_wait(probe_t *p, const action_t *a, system_t *sys) {
    (void)a; (void)sys;
    /* Minimal energy draw */
    p->energy_joules -= ENERGY_IDLE_PER_TICK;
    if (p->energy_joules < 0) p->energy_joules = 0;
    return ok(true);
}

static action_result_t exec_repair(probe_t *p, const action_t *a, system_t *sys) {
    (void)a; (void)sys;
    if (p->hull_integrity >= 1.0f)
        return fail("Hull already at full integrity");

    /* Repair costs energy and some iron */
    double iron_cost = 10.0;
    if (p->resources[RES_IRON] < iron_cost)
        return fail("Need iron for repairs");
    if (p->energy_joules < 1.0e9)
        return fail("Need energy for repairs");

    p->resources[RES_IRON] -= iron_cost;
    p->energy_joules -= 1.0e9;
    p->hull_integrity += 0.05f;
    if (p->hull_integrity > 1.0f) p->hull_integrity = 1.0f;

    return ok(true);
}

action_result_t probe_execute_action(probe_t *probe, const action_t *action,
                                      system_t *sys) {
    /* Dead probes can't act */
    if (probe->status == STATUS_DESTROYED)
        return fail("Probe is destroyed");

    switch (action->type) {
        case ACT_ENTER_ORBIT:       return exec_enter_orbit(probe, action, sys);
        case ACT_LAND:              return exec_land(probe, action, sys);
        case ACT_LAUNCH:            return exec_launch(probe, action, sys);
        case ACT_NAVIGATE_TO_BODY:  return exec_navigate_to_body(probe, action, sys);
        case ACT_SURVEY:            return exec_survey(probe, action, sys);
        case ACT_MINE:              return exec_mine(probe, action, sys);
        case ACT_WAIT:              return exec_wait(probe, action, sys);
        case ACT_REPAIR:            return exec_repair(probe, action, sys);
        default:                    return fail("Unknown action type");
    }
}

/* ---- Probe persistence ---- */

int persist_save_probe(void *persist, const probe_t *probe) {
    persist_t *p = (persist_t *)persist;
    if (!p || !p->db) return -1;

    sqlite3_stmt *stmt;
    const char *sql = "INSERT OR REPLACE INTO probes (id, parent_id, generation, data) "
                      "VALUES (?, ?, ?, ?);";
    if (sqlite3_prepare_v2(p->db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    /* ID as hex string */
    char id_str[33];
    snprintf(id_str, sizeof(id_str), "%016llx%016llx",
        (unsigned long long)probe->id.hi, (unsigned long long)probe->id.lo);
    sqlite3_bind_text(stmt, 1, id_str, -1, SQLITE_TRANSIENT);

    /* Parent ID */
    char parent_str[33];
    snprintf(parent_str, sizeof(parent_str), "%016llx%016llx",
        (unsigned long long)probe->parent_id.hi, (unsigned long long)probe->parent_id.lo);
    sqlite3_bind_text(stmt, 2, parent_str, -1, SQLITE_TRANSIENT);

    /* Generation */
    sqlite3_bind_int64(stmt, 3, probe->generation);

    /* Full probe struct as blob */
    sqlite3_bind_text(stmt, 4, (const char *)probe, (int)sizeof(probe_t), SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    sqlite3_finalize(stmt);
    return rc == SQLITE_DONE ? 0 : -1;
}

int persist_load_probe(void *persist, probe_uid_t id, probe_t *probe) {
    persist_t *p = (persist_t *)persist;
    if (!p || !p->db) return -1;

    sqlite3_stmt *stmt;
    const char *sql = "SELECT data FROM probes WHERE id = ?;";
    if (sqlite3_prepare_v2(p->db, sql, -1, &stmt, NULL) != SQLITE_OK)
        return -1;

    char id_str[33];
    snprintf(id_str, sizeof(id_str), "%016llx%016llx",
        (unsigned long long)id.hi, (unsigned long long)id.lo);
    sqlite3_bind_text(stmt, 1, id_str, -1, SQLITE_TRANSIENT);

    int rc = sqlite3_step(stmt);
    if (rc == SQLITE_ROW) {
        const void *blob = sqlite3_column_text(stmt, 0);
        if (blob) {
            memcpy(probe, blob, sizeof(probe_t));
            sqlite3_finalize(stmt);
            return 0;
        }
    }
    sqlite3_finalize(stmt);
    return -1;
}
