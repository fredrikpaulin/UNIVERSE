/*
 * universe.h — Core types for Project UNIVERSE
 *
 * All fundamental structs, enums, and constants live here.
 * This is the single source of truth for data layout.
 */
#ifndef UNIVERSE_H
#define UNIVERSE_H

#include <stdint.h>
#include <stdbool.h>
#include <stddef.h>

/* ---- Constants ---- */

#define MAX_PROBES       1024
#define MAX_PLANETS      16
#define MAX_STARS         3
#define MAX_MOONS        10
#define MAX_MODULES      16
#define MAX_NAME         64
#define MAX_MEMORIES     256
#define MAX_GOALS        32
#define MAX_QUIRKS       8
#define MAX_QUIRK_LEN    128
#define MAX_RELATIONSHIPS 64
#define MAX_CATCHPHRASES  8
#define MAX_VALUES        8
#define MAX_EARTH_MEM     16
#define MAX_EARTH_MEM_LEN 256
#define TICKS_PER_CYCLE   365
#define CYCLES_PER_EPOCH  1000

/* ---- Enums ---- */

typedef enum {
    STAR_O, STAR_B, STAR_A, STAR_F, STAR_G, STAR_K, STAR_M,
    STAR_WHITE_DWARF, STAR_NEUTRON, STAR_BLACK_HOLE,
    STAR_CLASS_COUNT
} star_class_t;

typedef enum {
    PLANET_GAS_GIANT, PLANET_ICE_GIANT, PLANET_ROCKY, PLANET_SUPER_EARTH,
    PLANET_OCEAN, PLANET_LAVA, PLANET_DESERT, PLANET_ICE,
    PLANET_CARBON, PLANET_IRON, PLANET_ROGUE,
    PLANET_TYPE_COUNT
} planet_type_t;

typedef enum {
    RES_IRON, RES_SILICON, RES_RARE_EARTH, RES_WATER,
    RES_HYDROGEN, RES_HELIUM3, RES_CARBON, RES_URANIUM,
    RES_EXOTIC,
    RES_COUNT
} resource_t;

typedef enum {
    TECH_PROPULSION, TECH_SENSORS, TECH_MINING, TECH_CONSTRUCTION,
    TECH_COMPUTING, TECH_ENERGY, TECH_MATERIALS, TECH_COMMUNICATION,
    TECH_WEAPONS, TECH_BIOTECH,
    TECH_COUNT
} tech_domain_t;

typedef enum {
    LOC_INTERSTELLAR, LOC_IN_SYSTEM, LOC_ORBITING, LOC_LANDED, LOC_DOCKED
} location_type_t;

typedef enum {
    STATUS_ACTIVE, STATUS_TRAVELING, STATUS_MINING, STATUS_BUILDING,
    STATUS_REPLICATING, STATUS_DORMANT, STATUS_DAMAGED, STATUS_DESTROYED
} probe_status_t;

typedef enum {
    EVT_DISCOVERY, EVT_ANOMALY, EVT_HAZARD, EVT_ENCOUNTER,
    EVT_CRISIS, EVT_WONDER, EVT_MESSAGE, EVT_REPLICATION,
    EVT_TYPE_COUNT
} event_type_t;

/* ---- Core Structs ---- */

typedef struct {
    uint64_t hi, lo;
} probe_uid_t;

typedef struct {
    int32_t x, y, z;
} sector_coord_t;

typedef struct {
    double x, y, z;
} vec3_t;

typedef struct {
    float curiosity, caution, sociability, humor, empathy;
    float ambition, creativity, stubbornness;
    float existential_angst, nostalgia_for_earth;
    float drift_rate;
} personality_traits_t;

typedef struct {
    probe_uid_t   id;
    char    name[MAX_NAME];
    star_class_t class;
    double  mass_solar;       /* in solar masses */
    double  luminosity_solar; /* in solar luminosities */
    double  temperature_k;
    double  age_gyr;          /* billions of years */
    double  metallicity;      /* [Fe/H] relative to solar */
    vec3_t  position;         /* within sector, in ly */
} star_t;

typedef struct {
    probe_uid_t        id;
    char         name[MAX_NAME];
    planet_type_t type;
    double       mass_earth;
    double       radius_earth;
    double       orbital_radius_au;
    double       orbital_period_days;
    double       eccentricity;
    double       axial_tilt_deg;
    double       rotation_period_hours;
    double       surface_temp_k;
    double       atmosphere_pressure_atm;
    double       water_coverage;         /* 0-1 */
    double       habitability_index;     /* 0-1 */
    double       magnetic_field;         /* relative to Earth */
    float        resources[RES_COUNT];   /* abundance 0-1 */
    bool         rings;
    bool         surveyed[5];            /* survey levels 0-4 completed */
    probe_uid_t        discovered_by;
    uint64_t     discovery_tick;
    uint8_t      moon_count;
    /* moons stored separately to keep struct flat */
    /* Alien artifacts (Phase 10) */
    bool         has_artifact;
    uint8_t      artifact_type;      /* 0=tech_boost, 1=resource_cache, 2=star_map, 3=comm_amplifier */
    uint8_t      artifact_tech_domain; /* for tech_boost: tech_domain_t */
    double       artifact_value;     /* bonus magnitude */
    char         artifact_desc[128];
    bool         artifact_discovered;
} planet_t;

typedef struct {
    probe_uid_t        id;
    char         name[MAX_NAME];
    sector_coord_t sector;
    vec3_t       position;       /* galactic position in ly */
    uint8_t      star_count;
    star_t       stars[MAX_STARS];
    uint8_t      planet_count;
    planet_t     planets[MAX_PLANETS];
    bool         visited;
    uint64_t     first_visit_tick;
} system_t;

typedef struct {
    uint64_t tick;
    char     event[256];
    float    emotional_weight;   /* 0-1, higher = more significant */
    float    fading;             /* 0 = vivid, 1 = nearly forgotten */
} memory_t;

typedef struct {
    char     description[256];
    float    priority;
    uint8_t  status;             /* 0=active, 1=completed, 2=abandoned, 3=deferred */
} goal_t;

typedef struct {
    probe_uid_t    other_id;
    float    trust;              /* -1 to 1 */
    uint64_t last_contact_tick;
    uint8_t  disposition;        /* 0=allied, 1=friendly, 2=neutral, 3=wary, 4=hostile */
} relationship_t;

typedef struct {
    probe_uid_t               id;
    probe_uid_t               parent_id;
    uint32_t            generation;
    char                name[MAX_NAME];

    /* Position */
    sector_coord_t      sector;
    probe_uid_t               system_id;
    probe_uid_t               body_id;
    location_type_t     location_type;

    /* Motion */
    double              speed_c;
    vec3_t              heading;
    vec3_t              destination;
    double              travel_remaining_ly;

    /* Resources */
    double              resources[RES_COUNT];
    double              energy_joules;
    double              fuel_kg;
    double              mass_kg;
    float               hull_integrity;

    /* Capabilities */
    uint8_t             tech_levels[TECH_COUNT];
    float               max_speed_c;
    float               sensor_range_ly;
    float               mining_rate;
    float               construction_rate;
    float               compute_capacity;

    /* Personality */
    personality_traits_t personality;
    char                quirks[MAX_QUIRKS][MAX_QUIRK_LEN];
    uint8_t             quirk_count;
    char                catchphrases[MAX_CATCHPHRASES][MAX_QUIRK_LEN];
    uint8_t             catchphrase_count;
    char                values[MAX_VALUES][MAX_QUIRK_LEN];
    uint8_t             value_count;
    char                earth_memories[MAX_EARTH_MEM][MAX_EARTH_MEM_LEN];
    uint8_t             earth_memory_count;
    float               earth_memory_fidelity; /* 1.0 for gen 0, degrades */

    /* Memory & goals */
    memory_t            memories[MAX_MEMORIES];
    uint16_t            memory_count;
    goal_t              goals[MAX_GOALS];
    uint8_t             goal_count;
    relationship_t      relationships[MAX_RELATIONSHIPS];
    uint8_t             relationship_count;

    /* Status */
    probe_status_t      status;
    uint64_t            created_tick;
} probe_t;

/* ---- Simulation State ---- */

typedef struct {
    uint64_t        seed;
    uint64_t        tick;
    uint32_t        generation_version;   /* generation algorithm version */
    uint32_t        probe_count;
    probe_t         probes[MAX_PROBES];
    bool            running;
    bool            visual;
} universe_t;

/* ---- UID helpers ---- */

static inline bool uid_eq(probe_uid_t a, probe_uid_t b) {
    return a.hi == b.hi && a.lo == b.lo;
}

static inline probe_uid_t uid_null(void) {
    return (probe_uid_t){0, 0};
}

static inline bool uid_is_null(probe_uid_t id) {
    return id.hi == 0 && id.lo == 0;
}

#endif /* UNIVERSE_H */
