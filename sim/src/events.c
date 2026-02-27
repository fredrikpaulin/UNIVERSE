/*
 * events.c — Phase 9: Events & Encounters
 *
 * Event generation engine, hazard effects, alien life, personality integration.
 */

#include "events.h"
#include "generate.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

/* ---- Description tables ---- */

static const char *DISCOVERY_DESCS[] = {
    "Detected an unusual mineral deposit with rare isotope signatures",
    "Found a striking geological formation carved by ancient forces",
    "Discovered an ancient impact crater with exposed subsurface layers",
    "Located underground water reserves beneath the surface"
};

static const char *HAZARD_DESCS[] = {
    "Solar flare eruption — intense radiation wave incoming",
    "Asteroid on collision course — evasive action required",
    "Intense radiation burst from nearby stellar remnant"
};

static const char *ANOMALY_DESCS[] = {
    "Detected an unexplained signal — origin unknown, pattern non-natural",
    "Anomalous energy reading — does not match any known physics",
    "Found an artifact of clearly artificial origin — not of probe manufacture"
};

static const char *WONDER_DESCS[] = {
    "Binary sunset — two stars setting in perfect alignment, painting the sky",
    "Distant supernova visible — a star's death illuminating the void",
    "Pulsar beam sweeping past — a cosmic lighthouse in the dark",
    "Nebula glow — ionized gas clouds shimmering with stellar light"
};

static const char *CRISIS_DESCS[] = {
    "Critical system failure — core subsystem malfunction detected",
    "Resource contamination — stored materials degrading unexpectedly",
    "Existential threat detected — unknown force destabilizing local space"
};

static const char *ENCOUNTER_DESCS[] = {
    "Signs of life detected — biological signatures in surface readings"
};

/* ---- Civilization name fragments ---- */

static const char *CIV_PREFIXES[] = {
    "Zar", "Kol", "Vex", "Tho", "Nir", "Pho", "Kel", "Myr",
    "Ish", "Dro", "Fen", "Gal", "Xen", "Lur", "Bri", "Qua"
};
#define CIV_PREFIX_COUNT 16

static const char *CIV_SUFFIXES[] = {
    "ani", "oth", "ari", "ene", "umi", "axi", "oni", "eli",
    "ura", "ite", "oid", "esh", "ynn", "ath", "obe", "ica"
};
#define CIV_SUFFIX_COUNT 16

static const char *ARTIFACT_DESCS[] = {
    "Crumbling stone monolith with geometric carvings",
    "Metallic structure of unknown alloy, partially buried",
    "Underground chamber with faded wall markings",
    "Dormant beacon emitting faint periodic signals",
    "Fossilized remains of large biological organisms",
    "Ruined settlement with grid-pattern streets",
    "Crystal storage medium containing encoded data",
    "Orbital debris ring from a collapsed space structure",
    "Chemical residue suggesting advanced industrial processes",
    "Warning beacon in an ancient symbolic language"
};
#define ARTIFACT_DESC_COUNT 10

static const char *CULTURAL_TRAITS[] = {
    "collaborative", "isolationist", "expansionist", "spiritual",
    "scientific", "artistic", "militaristic", "agrarian",
    "nomadic", "hierarchical", "egalitarian", "mercantile"
};
#define CULTURAL_TRAIT_COUNT 12

/* ---- Init ---- */

void events_init(event_system_t *es) {
    memset(es, 0, sizeof(*es));
}

/* ---- Hazard effects ---- */

float hazard_solar_flare(probe_t *probe, float severity) {
    /* Base damage 0.1-0.3, reduced by materials tech */
    float base_damage = 0.1f + severity * 0.2f;
    float reduction = probe->tech_levels[TECH_MATERIALS] * 0.02f;
    float damage = base_damage - reduction;
    if (damage < 0.01f) damage = 0.01f;  /* minimum damage */
    probe->hull_integrity -= damage;
    if (probe->hull_integrity < 0.0f) probe->hull_integrity = 0.0f;
    return damage;
}

float hazard_asteroid(probe_t *probe, float severity) {
    /* Direct hit: 0.05-0.25 hull damage */
    float damage = 0.05f + severity * 0.2f;
    probe->hull_integrity -= damage;
    if (probe->hull_integrity < 0.0f) probe->hull_integrity = 0.0f;
    return damage;
}

float hazard_radiation(probe_t *probe, float severity) {
    /* Damages computing capacity */
    float damage = 0.05f + severity * 0.15f;
    probe->compute_capacity -= damage;
    if (probe->compute_capacity < 0.0f) probe->compute_capacity = 0.0f;
    return damage;
}

/* ---- Event generation ---- */

static float random_severity(rng_t *rng) {
    return (float)(rng_next(rng) % 1000) / 1000.0f;
}

static void log_event(event_system_t *es, event_type_t type, int subtype,
                      probe_uid_t probe_id, probe_uid_t system_id,
                      uint64_t tick, const char *desc, float severity) {
    if (es->count >= MAX_EVENT_LOG) return;
    sim_event_t *e = &es->events[es->count++];
    e->type = type;
    e->subtype = subtype;
    e->probe_id = probe_id;
    e->system_id = system_id;
    e->tick = tick;
    e->severity = severity;
    strncpy(e->description, desc, sizeof(e->description) - 1);
    e->description[sizeof(e->description) - 1] = '\0';
}

static void apply_personality_and_memory(probe_t *probe, event_type_t type,
                                         const char *desc, uint64_t tick,
                                         float severity) {
    /* Map event type to drift event */
    drift_event_t drift;
    float emotional_weight = 0.3f + severity * 0.5f;

    switch (type) {
    case EVT_DISCOVERY:
        drift = DRIFT_DISCOVERY;
        break;
    case EVT_ANOMALY:
        drift = DRIFT_ANOMALY;
        break;
    case EVT_HAZARD:
        drift = DRIFT_DAMAGE;
        emotional_weight = 0.5f + severity * 0.4f;
        break;
    case EVT_ENCOUNTER:
        /* Encounters boost empathy and curiosity specifically */
        drift = DRIFT_DISCOVERY;  /* closest match */
        probe->personality.empathy += 0.05f * probe->personality.drift_rate;
        probe->personality.curiosity += 0.05f * probe->personality.drift_rate;
        probe->personality.empathy = trait_clamp(probe->personality.empathy);
        probe->personality.curiosity = trait_clamp(probe->personality.curiosity);
        emotional_weight = 0.7f + severity * 0.3f;
        break;
    case EVT_CRISIS:
        drift = DRIFT_DAMAGE;
        emotional_weight = 0.8f + severity * 0.2f;
        break;
    case EVT_WONDER:
        drift = DRIFT_BEAUTIFUL_SYSTEM;
        /* Wonders also nudge nostalgia and existential_angst */
        probe->personality.nostalgia_for_earth += 0.03f * probe->personality.drift_rate;
        probe->personality.existential_angst += 0.02f * probe->personality.drift_rate;
        probe->personality.nostalgia_for_earth = trait_clamp(probe->personality.nostalgia_for_earth);
        probe->personality.existential_angst = trait_clamp(probe->personality.existential_angst);
        emotional_weight = 0.6f + severity * 0.3f;
        break;
    default:
        drift = DRIFT_DISCOVERY;
        break;
    }

    personality_drift(probe, drift);
    memory_record(probe, tick, desc, emotional_weight);
}

int events_generate(event_system_t *es, probe_t *probe,
                    event_type_t type, int subtype,
                    const system_t *sys, uint64_t tick, rng_t *rng) {
    float severity = random_severity(rng);
    const char *desc = "Unknown event";
    probe_uid_t sys_id = sys ? sys->id : uid_null();

    switch (type) {
    case EVT_DISCOVERY:
        if (subtype >= 0 && subtype < DISC_SUBTYPE_COUNT)
            desc = DISCOVERY_DESCS[subtype];
        severity = 0.2f + severity * 0.3f;  /* low-moderate */
        break;

    case EVT_HAZARD:
        if (subtype >= 0 && subtype < HAZ_SUBTYPE_COUNT)
            desc = HAZARD_DESCS[subtype];
        severity = 0.3f + severity * 0.7f;  /* moderate-high */
        /* Apply hazard effect */
        switch (subtype) {
        case HAZ_SOLAR_FLARE:   hazard_solar_flare(probe, severity); break;
        case HAZ_ASTEROID_COLLISION: hazard_asteroid(probe, severity); break;
        case HAZ_RADIATION_BURST: hazard_radiation(probe, severity); break;
        default: break;
        }
        break;

    case EVT_ANOMALY:
        if (subtype >= 0 && subtype < ANOM_SUBTYPE_COUNT)
            desc = ANOMALY_DESCS[subtype];
        severity = 0.3f + severity * 0.4f;
        /* Create persistent anomaly marker */
        if (es->anomaly_count < MAX_ANOMALIES) {
            anomaly_t *a = &es->anomalies[es->anomaly_count++];
            a->id = generate_uid(rng);
            a->system_id = sys_id;
            if (sys && sys->planet_count > 0) {
                int pi = (int)(rng_next(rng) % sys->planet_count);
                a->planet_id = sys->planets[pi].id;
            }
            a->subtype = (anomaly_subtype_t)subtype;
            strncpy(a->description, desc, sizeof(a->description) - 1);
            a->discovered_tick = tick;
            a->resolved = false;
        }
        break;

    case EVT_WONDER:
        if (subtype >= 0 && subtype < WONDER_SUBTYPE_COUNT)
            desc = WONDER_DESCS[subtype];
        severity = 0.4f + severity * 0.3f;
        break;

    case EVT_CRISIS:
        if (subtype >= 0 && subtype < CRISIS_SUBTYPE_COUNT)
            desc = CRISIS_DESCS[subtype];
        severity = 0.6f + severity * 0.4f;  /* always high */
        /* Crisis causes hull damage + resource loss */
        probe->hull_integrity -= 0.1f * severity;
        if (probe->hull_integrity < 0.0f) probe->hull_integrity = 0.0f;
        break;

    case EVT_ENCOUNTER:
        desc = ENCOUNTER_DESCS[0];
        severity = 0.5f + severity * 0.4f;
        /* May generate civilization if system has habitable planet */
        if (sys) {
            for (int i = 0; i < sys->planet_count; i++) {
                if (sys->planets[i].habitability_index > 0.3) {
                    civilization_t civ;
                    if (alien_generate_civ(&civ, &sys->planets[i],
                                           probe->id, tick, rng) == 0) {
                        if (es->civ_count < MAX_CIVILIZATIONS) {
                            es->civilizations[es->civ_count++] = civ;
                        }
                        break;
                    }
                }
            }
        }
        break;

    default:
        return -1;
    }

    log_event(es, type, subtype, probe->id, sys_id, tick, desc, severity);
    apply_personality_and_memory(probe, type, desc, tick, severity);

    return 0;
}

/* ---- Per-tick event rolling ---- */

int events_tick_probe(event_system_t *es, probe_t *probe,
                      const system_t *current_system,
                      uint64_t tick, rng_t *rng) {
    /* Only generate events for probes in a system */
    if (!current_system) return 0;
    if (probe->status == STATUS_DESTROYED) return 0;

    int generated = 0;

    /* Roll for each event type */
    struct { event_type_t type; double freq; int subtype_count; } rolls[] = {
        { EVT_DISCOVERY, FREQ_DISCOVERY, DISC_SUBTYPE_COUNT },
        { EVT_ANOMALY,   FREQ_ANOMALY,   ANOM_SUBTYPE_COUNT },
        { EVT_HAZARD,    FREQ_HAZARD,    HAZ_SUBTYPE_COUNT },
        { EVT_ENCOUNTER, FREQ_ENCOUNTER, 1 },
        { EVT_CRISIS,    FREQ_CRISIS,    CRISIS_SUBTYPE_COUNT },
        { EVT_WONDER,    FREQ_WONDER,    WONDER_SUBTYPE_COUNT },
    };
    int roll_count = 6;

    for (int i = 0; i < roll_count && generated < MAX_EVENTS_PER_TICK; i++) {
        /* Random roll: probability check */
        double roll = (double)(rng_next(rng) % 1000000) / 1000000.0;
        if (roll < rolls[i].freq) {
            int subtype = (int)(rng_next(rng) % rolls[i].subtype_count);
            events_generate(es, probe, rolls[i].type, subtype,
                           current_system, tick, rng);
            generated++;
        }
    }

    return generated;
}

/* ---- Alien life ---- */

int alien_check_planet(const planet_t *planet, rng_t *rng) {
    /* Base chance proportional to habitability. 1/10000 at full habitability. */
    double base_chance = planet->habitability_index * 0.0001;

    /* Water coverage boosts chance */
    base_chance *= (1.0 + planet->water_coverage);

    /* Rocky planets more likely to have life */
    if (planet->type == PLANET_ROCKY || planet->type == PLANET_SUPER_EARTH ||
        planet->type == PLANET_OCEAN) {
        base_chance *= 2.0;
    }

    double roll = (double)(rng_next(rng) % 1000000) / 1000000.0;
    if (roll >= base_chance) return -1;

    /* Determine civ type — weighted distribution */
    double type_roll = (double)(rng_next(rng) % 1000) / 1000.0;
    if (type_roll < 0.40) return CIV_MICROBIAL;
    if (type_roll < 0.60) return CIV_MULTICELLULAR;
    if (type_roll < 0.75) return CIV_COMPLEX_ECOSYSTEM;
    if (type_roll < 0.82) return CIV_PRE_TOOL;
    if (type_roll < 0.87) return CIV_TOOL_USING;
    if (type_roll < 0.90) return CIV_PRE_INDUSTRIAL;
    if (type_roll < 0.93) return CIV_EXTINCT;       /* dead civs common */
    if (type_roll < 0.95) return CIV_INDUSTRIAL;
    if (type_roll < 0.97) return CIV_INFORMATION_AGE;
    if (type_roll < 0.98) return CIV_SPACEFARING;
    if (type_roll < 0.99) return CIV_ADVANCED_SPACEFARING;
    if (type_roll < 0.995) return CIV_POST_BIOLOGICAL;
    return CIV_TRANSCENDED;
}

int alien_generate_civ(civilization_t *civ, const planet_t *planet,
                       probe_uid_t discovered_by, uint64_t tick, rng_t *rng) {
    int type = alien_check_planet(planet, rng);
    if (type < 0) return -1;

    memset(civ, 0, sizeof(*civ));
    civ->id = generate_uid(rng);
    civ->homeworld_id = planet->id;
    civ->type = (civ_type_t)type;
    civ->discovered_tick = tick;
    civ->discovered_by = discovered_by;

    /* Generate name */
    int pi = (int)(rng_next(rng) % CIV_PREFIX_COUNT);
    int si = (int)(rng_next(rng) % CIV_SUFFIX_COUNT);
    snprintf(civ->name, MAX_CIV_NAME, "%s%s", CIV_PREFIXES[pi], CIV_SUFFIXES[si]);

    /* Disposition */
    if (type <= CIV_COMPLEX_ECOSYSTEM) {
        civ->disposition = DISP_UNAWARE;
    } else {
        civ->disposition = (civ_disposition_t)(rng_next(rng) % DISP_COUNT);
    }

    /* Tech level scales with civ type */
    static const uint8_t TYPE_BASE_TECH[] = {
        0, 0, 0,    /* microbial, multicellular, complex_ecosystem */
        1, 2, 3,    /* pre_tool, tool_using, pre_industrial */
        5, 8, 12,   /* industrial, information, spacefaring */
        16, 18,     /* advanced_spacefaring, post_biological */
        0, 20       /* extinct (was something), transcended */
    };
    civ->tech_level = TYPE_BASE_TECH[type];
    if (type == CIV_EXTINCT) {
        /* Extinct civs had random tech when alive */
        civ->tech_level = (uint8_t)(3 + rng_next(rng) % 15);
    }

    /* Biology */
    double bio_roll = (double)(rng_next(rng) % 100) / 100.0;
    if (bio_roll < 0.70) civ->biology_base = BIO_CARBON;
    else if (bio_roll < 0.85) civ->biology_base = BIO_SILICON;
    else if (bio_roll < 0.95) civ->biology_base = BIO_AMMONIA;
    else civ->biology_base = BIO_EXOTIC;

    /* State */
    if (type == CIV_EXTINCT) {
        civ->state = CIV_STATE_EXTINCT;
    } else if (type == CIV_TRANSCENDED) {
        civ->state = CIV_ASCENDING;
    } else {
        double state_roll = (double)(rng_next(rng) % 100) / 100.0;
        if (state_roll < 0.50) civ->state = CIV_THRIVING;
        else if (state_roll < 0.70) civ->state = CIV_DECLINING;
        else if (state_roll < 0.85) civ->state = CIV_ENDANGERED;
        else if (state_roll < 0.95) civ->state = CIV_STATE_EXTINCT;
        else civ->state = CIV_ASCENDING;
    }

    /* Artifacts — extinct civs always have some, others maybe */
    int artifact_count = 0;
    if (civ->state == CIV_STATE_EXTINCT || type == CIV_EXTINCT) {
        artifact_count = 2 + (int)(rng_next(rng) % 4);  /* 2-5 */
    } else if (civ->tech_level >= 5) {
        artifact_count = (int)(rng_next(rng) % 3);  /* 0-2 */
    }
    if (artifact_count > MAX_ARTIFACTS) artifact_count = MAX_ARTIFACTS;
    civ->artifact_count = (uint8_t)artifact_count;
    for (int i = 0; i < artifact_count; i++) {
        int ai = (int)(rng_next(rng) % ARTIFACT_DESC_COUNT);
        strncpy(civ->artifacts[i], ARTIFACT_DESCS[ai], MAX_ARTIFACT_DESC - 1);
    }

    /* Cultural traits */
    int trait_count = 1 + (int)(rng_next(rng) % MAX_CULTURAL_TRAITS);
    civ->cultural_trait_count = (uint8_t)trait_count;
    for (int i = 0; i < trait_count; i++) {
        int ci = (int)(rng_next(rng) % CULTURAL_TRAIT_COUNT);
        strncpy(civ->cultural_traits[i], CULTURAL_TRAITS[ci], MAX_CULTURAL_TRAIT_LEN - 1);
    }

    return 0;
}

/* ---- Queries ---- */

int events_get_for_probe(const event_system_t *es, probe_uid_t probe_id,
                         sim_event_t *out, int max_out) {
    int count = 0;
    for (int i = 0; i < es->count && count < max_out; i++) {
        if (uid_eq(es->events[i].probe_id, probe_id)) {
            out[count++] = es->events[i];
        }
    }
    return count;
}

int events_get_anomalies(const event_system_t *es, probe_uid_t system_id,
                         anomaly_t *out, int max_out) {
    int count = 0;
    for (int i = 0; i < es->anomaly_count && count < max_out; i++) {
        if (uid_eq(es->anomalies[i].system_id, system_id) &&
            !es->anomalies[i].resolved) {
            out[count++] = es->anomalies[i];
        }
    }
    return count;
}

const civilization_t *events_get_civ(const event_system_t *es,
                                     probe_uid_t planet_id) {
    for (int i = 0; i < es->civ_count; i++) {
        if (uid_eq(es->civilizations[i].homeworld_id, planet_id)) {
            return &es->civilizations[i];
        }
    }
    return NULL;
}

/* ---- Determinism check ---- */

bool events_deterministic_check(uint64_t seed, int tick_count,
                                event_type_t *out_types, int *out_count,
                                int max_out) {
    event_system_t es;
    events_init(&es);
    rng_t rng;
    rng_seed(&rng, seed);

    probe_t probe = {0};
    probe.id = (probe_uid_t){0, 1};
    probe.location_type = LOC_IN_SYSTEM;
    probe.status = STATUS_ACTIVE;
    probe.hull_integrity = 1.0f;
    probe.energy_joules = 1e12;
    probe.compute_capacity = 1.0f;
    probe.tech_levels[TECH_MATERIALS] = 5;
    probe.personality.drift_rate = 1.0f;

    system_t sys = {0};
    sys.id = (probe_uid_t){0, 100};
    sys.star_count = 1;
    sys.planet_count = 1;
    sys.planets[0].type = PLANET_ROCKY;
    sys.planets[0].habitability_index = 0.5;

    *out_count = 0;
    for (int t = 0; t < tick_count; t++) {
        /* Keep probe alive */
        probe.hull_integrity = 1.0f;
        probe.compute_capacity = 1.0f;

        int before = es.count;
        events_tick_probe(&es, &probe, &sys, (uint64_t)t, &rng);

        for (int i = before; i < es.count && *out_count < max_out; i++) {
            out_types[(*out_count)++] = es.events[i].type;
        }
    }

    return true;
}
