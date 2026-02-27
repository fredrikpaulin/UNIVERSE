#ifndef EVENTS_H
#define EVENTS_H

#include "universe.h"
#include "rng.h"
#include "personality.h"

/* ---- Constants ---- */

#define MAX_EVENTS_PER_TICK  8
#define MAX_EVENT_LOG       512
#define MAX_ANOMALIES       256
#define MAX_CIVILIZATIONS   128
#define MAX_ARTIFACTS        64
#define MAX_ARTIFACT_DESC   128
#define MAX_CIV_NAME         64
#define MAX_CULTURAL_TRAITS   4
#define MAX_CULTURAL_TRAIT_LEN 64

/* ---- Event frequencies (per-tick probability while in-system) ---- */

#define FREQ_DISCOVERY     0.005    /* common */
#define FREQ_ANOMALY       0.001    /* uncommon */
#define FREQ_HAZARD        0.002    /* moderate */
#define FREQ_ENCOUNTER     0.0002   /* rare */
#define FREQ_CRISIS        0.00005  /* very rare */
#define FREQ_WONDER        0.0003   /* rare */

/* ---- Event subtypes ---- */

typedef enum {
    /* Discovery subtypes */
    DISC_MINERAL_DEPOSIT,
    DISC_GEOLOGICAL_FORMATION,
    DISC_IMPACT_CRATER,
    DISC_UNDERGROUND_WATER,
    DISC_SUBTYPE_COUNT
} discovery_subtype_t;

typedef enum {
    /* Hazard subtypes */
    HAZ_SOLAR_FLARE,
    HAZ_ASTEROID_COLLISION,
    HAZ_RADIATION_BURST,
    HAZ_SUBTYPE_COUNT
} hazard_subtype_t;

typedef enum {
    /* Anomaly subtypes */
    ANOM_UNEXPLAINED_SIGNAL,
    ANOM_ENERGY_READING,
    ANOM_ARTIFACT,
    ANOM_SUBTYPE_COUNT
} anomaly_subtype_t;

typedef enum {
    /* Wonder subtypes */
    WONDER_BINARY_SUNSET,
    WONDER_SUPERNOVA_VISIBLE,
    WONDER_PULSAR_BEAM,
    WONDER_NEBULA_GLOW,
    WONDER_SUBTYPE_COUNT
} wonder_subtype_t;

typedef enum {
    /* Crisis subtypes */
    CRISIS_SYSTEM_FAILURE,
    CRISIS_RESOURCE_CONTAMINATION,
    CRISIS_EXISTENTIAL_THREAT,
    CRISIS_SUBTYPE_COUNT
} crisis_subtype_t;

/* ---- Alien civilization types ---- */

typedef enum {
    CIV_MICROBIAL, CIV_MULTICELLULAR, CIV_COMPLEX_ECOSYSTEM,
    CIV_PRE_TOOL, CIV_TOOL_USING, CIV_PRE_INDUSTRIAL,
    CIV_INDUSTRIAL, CIV_INFORMATION_AGE, CIV_SPACEFARING,
    CIV_ADVANCED_SPACEFARING, CIV_POST_BIOLOGICAL,
    CIV_EXTINCT, CIV_TRANSCENDED,
    CIV_TYPE_COUNT
} civ_type_t;

typedef enum {
    DISP_UNAWARE, DISP_CURIOUS, DISP_CAUTIOUS,
    DISP_WELCOMING, DISP_HOSTILE, DISP_INDIFFERENT,
    DISP_COUNT
} civ_disposition_t;

typedef enum {
    BIO_CARBON, BIO_SILICON, BIO_AMMONIA, BIO_EXOTIC,
    BIO_BASE_COUNT
} bio_base_t;

typedef enum {
    CIV_THRIVING, CIV_DECLINING, CIV_ENDANGERED,
    CIV_STATE_EXTINCT, CIV_ASCENDING,
    CIV_STATE_COUNT
} civ_state_t;

/* ---- Structs ---- */

typedef struct {
    event_type_t type;
    int          subtype;        /* cast to appropriate subtype enum */
    probe_uid_t  probe_id;       /* which probe experienced it */
    probe_uid_t  system_id;      /* where it happened */
    uint64_t     tick;
    char         description[256];
    float        severity;       /* 0-1, how impactful */
} sim_event_t;

typedef struct {
    probe_uid_t  id;
    probe_uid_t  system_id;
    probe_uid_t  planet_id;
    anomaly_subtype_t subtype;
    char         description[256];
    uint64_t     discovered_tick;
    bool         resolved;
} anomaly_t;

typedef struct {
    probe_uid_t     id;
    char            name[MAX_CIV_NAME];
    probe_uid_t     homeworld_id;
    civ_type_t      type;
    civ_disposition_t disposition;
    uint8_t         tech_level;     /* 0-20 */
    bio_base_t      biology_base;
    civ_state_t     state;
    char            artifacts[MAX_ARTIFACTS][MAX_ARTIFACT_DESC];
    uint8_t         artifact_count;
    char            cultural_traits[MAX_CULTURAL_TRAITS][MAX_CULTURAL_TRAIT_LEN];
    uint8_t         cultural_trait_count;
    uint64_t        discovered_tick;
    probe_uid_t     discovered_by;
} civilization_t;

/* ---- Event log ---- */

typedef struct {
    sim_event_t    events[MAX_EVENT_LOG];
    int            count;
    anomaly_t      anomalies[MAX_ANOMALIES];
    int            anomaly_count;
    civilization_t civilizations[MAX_CIVILIZATIONS];
    int            civ_count;
} event_system_t;

/* ---- API ---- */

/* Initialize event system */
void events_init(event_system_t *es);

/* Roll for events for a single probe this tick.
 * Fires personality drift and records memories for triggered events.
 * Returns number of events generated. */
int events_tick_probe(event_system_t *es, probe_t *probe,
                      const system_t *current_system,
                      uint64_t tick, rng_t *rng);

/* Generate a specific event type (for testing/scripting).
 * Returns 0 on success. */
int events_generate(event_system_t *es, probe_t *probe,
                    event_type_t type, int subtype,
                    const system_t *sys, uint64_t tick, rng_t *rng);

/* ---- Hazard effects ---- */

/* Apply solar flare damage. Hull damage proportional to severity.
 * Materials tech reduces damage. */
float hazard_solar_flare(probe_t *probe, float severity);

/* Apply asteroid collision. Direct hull hit. */
float hazard_asteroid(probe_t *probe, float severity);

/* Apply radiation burst. Damages computing capacity. */
float hazard_radiation(probe_t *probe, float severity);

/* ---- Alien life ---- */

/* Check if a planet has alien life (deterministic for planet seed).
 * Returns civ type, or -1 if no life. */
int alien_check_planet(const planet_t *planet, rng_t *rng);

/* Generate a full civilization for a planet.
 * Returns 0 on success, -1 if no life generated. */
int alien_generate_civ(civilization_t *civ, const planet_t *planet,
                       probe_uid_t discovered_by, uint64_t tick, rng_t *rng);

/* ---- Query ---- */

/* Get events for a specific probe from the log. Returns count. */
int events_get_for_probe(const event_system_t *es, probe_uid_t probe_id,
                         sim_event_t *out, int max_out);

/* Get anomalies in a system. Returns count. */
int events_get_anomalies(const event_system_t *es, probe_uid_t system_id,
                         anomaly_t *out, int max_out);

/* Get civilization on a planet, or NULL if none. */
const civilization_t *events_get_civ(const event_system_t *es,
                                     probe_uid_t planet_id);

/* Check if event type is deterministic for seed (replay test). */
bool events_deterministic_check(uint64_t seed, int tick_count,
                                event_type_t *out_types, int *out_count,
                                int max_out);

#endif /* EVENTS_H */
