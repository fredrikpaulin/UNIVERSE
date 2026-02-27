/*
 * generate.c — Procedural galaxy generation
 *
 * Stars follow real HR diagram distributions.
 * Planets generated per star using simplified accretion model.
 * Everything deterministic from galaxy_seed + coordinates.
 */
#include "generate.h"
#include "util.h"
#include <math.h>
#include <string.h>

#ifndef M_PI
#define M_PI 3.14159265358979323846
#endif

/* ---- Star class distribution (cumulative, from spec section 2.3) ---- */

/* Frequency: M=76.5%, K=12.1%, G=7.6%, F=3%, A=0.6%, B=0.13%, O=0.00003%
 * Plus small chances for remnants */
static const struct {
    star_class_t class;
    double       cumulative;  /* cumulative probability */
    double       temp_lo, temp_hi;
    double       mass_lo, mass_hi;     /* solar masses */
    double       lum_lo, lum_hi;       /* solar luminosities */
} STAR_TABLE[] = {
    { STAR_M,           0.7650, 2400, 3700,   0.08, 0.45,   0.0001, 0.08 },
    { STAR_K,           0.8860, 3700, 5200,   0.45, 0.80,   0.08,   0.60 },
    { STAR_G,           0.9620, 5200, 6000,   0.80, 1.04,   0.60,   1.50 },
    { STAR_F,           0.9920, 6000, 7500,   1.04, 1.40,   1.50,   5.00 },
    { STAR_A,           0.9980, 7500, 10000,  1.40, 2.10,   5.00,  25.00 },
    { STAR_B,           0.9993, 10000, 30000, 2.10, 16.0,  25.00, 30000.0 },
    { STAR_O,           0.99933, 30000, 50000, 16.0, 90.0, 30000.0, 1000000.0 },
    { STAR_WHITE_DWARF, 0.9998, 4000, 40000,  0.17, 1.33,  0.0001, 0.10 },
    { STAR_NEUTRON,     0.99998, 0, 0,        1.10, 2.16,   0.0,    0.0  },
    { STAR_BLACK_HOLE,  1.0000, 0, 0,         3.0, 100.0,   0.0,    0.0  },
};
#define STAR_TABLE_LEN (sizeof(STAR_TABLE) / sizeof(STAR_TABLE[0]))

/* ---- Star name syllables for procedural naming ---- */

static const char *NAME_PREFIX[] = {
    "Al", "Be", "Ca", "De", "El", "Fa", "Ga", "He", "In", "Jo",
    "Ka", "Le", "Ma", "Ne", "Or", "Pa", "Qu", "Re", "Sa", "Te",
    "Um", "Ve", "Wa", "Xe", "Ya", "Ze", "Ar", "Bo", "Cy", "Di",
    "Et", "Fi", "Gi", "Ha", "Ix", "Ju", "Ko", "Li", "Mi", "No",
};
static const char *NAME_MIDDLE[] = {
    "ra", "le", "ni", "ta", "so", "mu", "ka", "ri", "do", "ve",
    "na", "li", "pe", "tu", "go", "sa", "mi", "fe", "ba", "lo",
    "ne", "si", "ru", "wa", "ke", "di", "mo", "pa", "ti", "xu",
};
static const char *NAME_SUFFIX[] = {
    "x", "n", "s", "r", "th", "m", "l", "d", "k", "ph",
    "ris", "nus", "tis", "lon", "sar", "mir", "dex", "vos", "pis", "tar",
};
#define NAME_PREFIX_LEN ARRAY_LEN(NAME_PREFIX)
#define NAME_MIDDLE_LEN ARRAY_LEN(NAME_MIDDLE)
#define NAME_SUFFIX_LEN ARRAY_LEN(NAME_SUFFIX)

/* ---- Spiral arm model ---- */

/* 4-arm logarithmic spiral. Returns density factor 0-1 based on
 * how close a galactic (x,y) position is to a spiral arm. */
static double spiral_arm_density(double gx, double gy) {
    double r = sqrt(gx * gx + gy * gy);
    if (r < 100.0) return 1.0;  /* dense core */

    double theta = atan2(gy, gx);
    double best = 0.0;

    /* 4 arms, each offset by pi/2 */
    for (int arm = 0; arm < 4; arm++) {
        double arm_offset = arm * (M_PI / 2.0);
        /* Logarithmic spiral: theta = a * ln(r/r0) + offset
         * We check how close our theta is to the arm's theta at radius r */
        double pitch = 0.22;  /* pitch angle ~12.6 degrees */
        double arm_theta = pitch * log(r / 1000.0) + arm_offset;

        /* Angular distance (wrapped to [-pi, pi]) */
        double diff = theta - arm_theta;
        diff = fmod(diff + 3.0 * M_PI, 2.0 * M_PI) - M_PI;

        /* Gaussian falloff from arm center */
        double arm_width = 0.4;  /* radians */
        double density = exp(-(diff * diff) / (2.0 * arm_width * arm_width));
        if (density > best) best = density;
    }

    /* Base density (inter-arm) plus arm bonus */
    double base = 0.15;
    /* Density falls off with distance from center */
    double radial_falloff = exp(-r / 40000.0);

    return (base + (1.0 - base) * best) * radial_falloff;
}

/* ---- Helpers ---- */

static double lerp(double a, double b, double t) {
    return a + (b - a) * t;
}

probe_uid_t generate_uid(rng_t *rng) {
    return (probe_uid_t){ rng_next(rng), rng_next(rng) };
}

static void generate_name(char *buf, size_t buflen, rng_t *rng) {
    int pre = (int)rng_range(rng, NAME_PREFIX_LEN);
    int mid = (int)rng_range(rng, NAME_MIDDLE_LEN);
    int suf = (int)rng_range(rng, NAME_SUFFIX_LEN);
    int has_middle = rng_double(rng) < 0.6;

    if (has_middle)
        snprintf(buf, buflen, "%s%s%s", NAME_PREFIX[pre], NAME_MIDDLE[mid], NAME_SUFFIX[suf]);
    else
        snprintf(buf, buflen, "%s%s", NAME_PREFIX[pre], NAME_SUFFIX[suf]);
}

/* ---- Star generation ---- */

static void generate_star(star_t *star, rng_t *rng, vec3_t pos) {
    star->id = generate_uid(rng);
    generate_name(star->name, MAX_NAME, rng);
    star->position = pos;

    /* Pick star class from distribution */
    double roll = rng_double(rng);
    for (int i = 0; i < (int)STAR_TABLE_LEN; i++) {
        if (roll <= STAR_TABLE[i].cumulative) {
            star->class = STAR_TABLE[i].class;
            double t = rng_double(rng);
            star->temperature_k = lerp(STAR_TABLE[i].temp_lo, STAR_TABLE[i].temp_hi, t);
            star->mass_solar = lerp(STAR_TABLE[i].mass_lo, STAR_TABLE[i].mass_hi, t);
            star->luminosity_solar = lerp(STAR_TABLE[i].lum_lo, STAR_TABLE[i].lum_hi, t);
            break;
        }
    }

    star->age_gyr = lerp(0.1, 13.0, rng_double(rng));
    star->metallicity = rng_gaussian(rng) * 0.3; /* [Fe/H] centered on 0 */
}

/* ---- Habitable zone ---- */

void habitable_zone(double luminosity_solar, double *inner_au, double *outer_au) {
    double sqrt_l = sqrt(luminosity_solar);
    *inner_au = sqrt_l * 0.95;
    *outer_au = sqrt_l * 1.37;
}

/* ---- Planet generation ---- */

static planet_type_t pick_planet_type(rng_t *rng, double orbital_au,
                                       double hz_inner, double hz_outer,
                                       star_class_t star_class) {
    (void)star_class;
    double r = rng_double(rng);

    if (orbital_au < hz_inner * 0.5) {
        /* Very close — hot zone */
        if (r < 0.3) return PLANET_LAVA;
        if (r < 0.6) return PLANET_IRON;
        if (r < 0.8) return PLANET_ROCKY;
        return PLANET_DESERT;
    } else if (orbital_au >= hz_inner && orbital_au <= hz_outer) {
        /* Habitable zone */
        if (r < 0.25) return PLANET_ROCKY;
        if (r < 0.45) return PLANET_OCEAN;
        if (r < 0.60) return PLANET_SUPER_EARTH;
        if (r < 0.75) return PLANET_DESERT;
        if (r < 0.85) return PLANET_CARBON;
        return PLANET_ICE;
    } else if (orbital_au < hz_inner) {
        /* Warm zone */
        if (r < 0.35) return PLANET_ROCKY;
        if (r < 0.55) return PLANET_DESERT;
        if (r < 0.70) return PLANET_SUPER_EARTH;
        if (r < 0.85) return PLANET_LAVA;
        return PLANET_IRON;
    } else if (orbital_au < hz_outer * 5.0) {
        /* Cold zone — gas/ice giants more likely */
        if (r < 0.35) return PLANET_GAS_GIANT;
        if (r < 0.55) return PLANET_ICE_GIANT;
        if (r < 0.70) return PLANET_ICE;
        if (r < 0.85) return PLANET_ROCKY;
        return PLANET_SUPER_EARTH;
    } else {
        /* Far outer — ice and gas */
        if (r < 0.40) return PLANET_ICE_GIANT;
        if (r < 0.65) return PLANET_GAS_GIANT;
        if (r < 0.80) return PLANET_ICE;
        if (r < 0.95) return PLANET_ROGUE;
        return PLANET_CARBON;
    }
}

/* Mass ranges by planet type (Earth masses) */
static void planet_mass_range(planet_type_t type, double *lo, double *hi) {
    switch (type) {
        case PLANET_GAS_GIANT:   *lo = 10.0;   *hi = 4000.0; break;
        case PLANET_ICE_GIANT:   *lo = 5.0;    *hi = 50.0;   break;
        case PLANET_ROCKY:       *lo = 0.01;   *hi = 2.0;    break;
        case PLANET_SUPER_EARTH: *lo = 1.5;    *hi = 10.0;   break;
        case PLANET_OCEAN:       *lo = 0.5;    *hi = 8.0;    break;
        case PLANET_LAVA:        *lo = 0.1;    *hi = 3.0;    break;
        case PLANET_DESERT:      *lo = 0.1;    *hi = 5.0;    break;
        case PLANET_ICE:         *lo = 0.01;   *hi = 5.0;    break;
        case PLANET_CARBON:      *lo = 0.5;    *hi = 8.0;    break;
        case PLANET_IRON:        *lo = 0.1;    *hi = 4.0;    break;
        case PLANET_ROGUE:       *lo = 0.001;  *hi = 15.0;   break;
        default:                 *lo = 0.1;    *hi = 2.0;    break;
    }
}

/* Approximate radius from mass (power law, differs by type) */
static double planet_radius(planet_type_t type, double mass_earth) {
    switch (type) {
        case PLANET_GAS_GIANT:
        case PLANET_ICE_GIANT:
            /* Gas/ice giants: radius grows slowly with mass */
            return pow(mass_earth, 0.06) * (type == PLANET_GAS_GIANT ? 11.0 : 4.0);
        default:
            /* Rocky/terrestrial: r ~ m^0.27 (rough fit) */
            return pow(mass_earth, 0.27);
    }
}

/* Resource generation based on planet type */
static void generate_resources(float resources[RES_COUNT], rng_t *rng,
                                planet_type_t type) {
    memset(resources, 0, sizeof(float) * RES_COUNT);

    /* Base resources by type */
    switch (type) {
        case PLANET_ROCKY:
        case PLANET_DESERT:
            resources[RES_IRON]       = 0.3f + 0.5f * (float)rng_double(rng);
            resources[RES_SILICON]    = 0.3f + 0.5f * (float)rng_double(rng);
            resources[RES_RARE_EARTH] = 0.05f + 0.15f * (float)rng_double(rng);
            resources[RES_CARBON]     = 0.05f + 0.1f * (float)rng_double(rng);
            resources[RES_URANIUM]    = 0.01f + 0.05f * (float)rng_double(rng);
            break;
        case PLANET_IRON:
            resources[RES_IRON]       = 0.6f + 0.4f * (float)rng_double(rng);
            resources[RES_SILICON]    = 0.1f + 0.2f * (float)rng_double(rng);
            resources[RES_RARE_EARTH] = 0.1f + 0.3f * (float)rng_double(rng);
            resources[RES_URANIUM]    = 0.03f + 0.1f * (float)rng_double(rng);
            break;
        case PLANET_OCEAN:
            resources[RES_WATER]      = 0.7f + 0.3f * (float)rng_double(rng);
            resources[RES_SILICON]    = 0.1f + 0.2f * (float)rng_double(rng);
            resources[RES_IRON]       = 0.05f + 0.15f * (float)rng_double(rng);
            break;
        case PLANET_ICE:
            resources[RES_WATER]      = 0.5f + 0.5f * (float)rng_double(rng);
            resources[RES_HYDROGEN]   = 0.1f + 0.2f * (float)rng_double(rng);
            resources[RES_HELIUM3]    = 0.01f + 0.05f * (float)rng_double(rng);
            break;
        case PLANET_GAS_GIANT:
            resources[RES_HYDROGEN]   = 0.7f + 0.3f * (float)rng_double(rng);
            resources[RES_HELIUM3]    = 0.1f + 0.3f * (float)rng_double(rng);
            break;
        case PLANET_ICE_GIANT:
            resources[RES_HYDROGEN]   = 0.3f + 0.3f * (float)rng_double(rng);
            resources[RES_WATER]      = 0.3f + 0.3f * (float)rng_double(rng);
            resources[RES_HELIUM3]    = 0.05f + 0.15f * (float)rng_double(rng);
            break;
        case PLANET_CARBON:
            resources[RES_CARBON]     = 0.6f + 0.4f * (float)rng_double(rng);
            resources[RES_SILICON]    = 0.1f + 0.2f * (float)rng_double(rng);
            resources[RES_RARE_EARTH] = 0.05f + 0.1f * (float)rng_double(rng);
            break;
        case PLANET_LAVA:
            resources[RES_IRON]       = 0.4f + 0.4f * (float)rng_double(rng);
            resources[RES_SILICON]    = 0.2f + 0.3f * (float)rng_double(rng);
            resources[RES_RARE_EARTH] = 0.1f + 0.2f * (float)rng_double(rng);
            break;
        case PLANET_SUPER_EARTH:
            resources[RES_IRON]       = 0.2f + 0.4f * (float)rng_double(rng);
            resources[RES_SILICON]    = 0.2f + 0.4f * (float)rng_double(rng);
            resources[RES_WATER]      = 0.1f + 0.3f * (float)rng_double(rng);
            resources[RES_RARE_EARTH] = 0.05f + 0.15f * (float)rng_double(rng);
            resources[RES_CARBON]     = 0.05f + 0.15f * (float)rng_double(rng);
            break;
        case PLANET_ROGUE:
            resources[RES_WATER]      = 0.1f + 0.3f * (float)rng_double(rng);
            resources[RES_IRON]       = 0.1f + 0.2f * (float)rng_double(rng);
            break;
        default:
            break;
    }

    /* Rare exotic matter — very low chance on any planet */
    if (rng_double(rng) < 0.005) {
        resources[RES_EXOTIC] = 0.01f + 0.05f * (float)rng_double(rng);
    }
}

static void generate_planet(planet_t *p, rng_t *rng, int index,
                             star_t *star) {
    p->id = generate_uid(rng);

    /* Procedural planet name: star name + roman numeral-ish suffix */
    char suffix[8];
    snprintf(suffix, sizeof(suffix), " %c", 'b' + index);
    snprintf(p->name, MAX_NAME, "%s%s", star->name, suffix);

    /* Orbital radius: Titius-Bode-ish distribution */
    double base_au;
    if (index == 0) {
        base_au = 0.1 + 0.3 * rng_double(rng);
    } else {
        /* Each planet roughly 1.4-2.2x further than previous.
         * We use index to compute this deterministically. */
        base_au = (0.2 + 0.2 * rng_double(rng)) * pow(1.4 + 0.8 * rng_double(rng), index);
    }
    /* Scale by star luminosity (brighter stars → wider spacing) */
    p->orbital_radius_au = base_au * sqrt(MAX(star->luminosity_solar, 0.01));

    /* Habitable zone */
    double hz_inner, hz_outer;
    habitable_zone(star->luminosity_solar, &hz_inner, &hz_outer);

    /* Planet type */
    p->type = pick_planet_type(rng, p->orbital_radius_au, hz_inner, hz_outer, star->class);

    /* Mass */
    double m_lo, m_hi;
    planet_mass_range(p->type, &m_lo, &m_hi);
    p->mass_earth = lerp(m_lo, m_hi, rng_double(rng));

    /* Radius */
    p->radius_earth = planet_radius(p->type, p->mass_earth);

    /* Orbital period: Kepler's third law. P^2 = a^3 / M_star (in solar units, years) */
    double a3 = p->orbital_radius_au * p->orbital_radius_au * p->orbital_radius_au;
    double period_years = sqrt(a3 / MAX(star->mass_solar, 0.01));
    p->orbital_period_days = period_years * 365.25;

    /* Orbital parameters */
    p->eccentricity = rng_double(rng) * 0.3; /* most orbits fairly circular */
    if (rng_double(rng) < 0.05) p->eccentricity = 0.3 + rng_double(rng) * 0.5; /* rare eccentric */
    p->axial_tilt_deg = rng_double(rng) * 45.0;
    if (rng_double(rng) < 0.1) p->axial_tilt_deg = 45.0 + rng_double(rng) * 135.0; /* rare extreme tilt */
    p->rotation_period_hours = 5.0 + rng_double(rng) * 200.0;
    if (p->type == PLANET_GAS_GIANT || p->type == PLANET_ICE_GIANT)
        p->rotation_period_hours = 8.0 + rng_double(rng) * 20.0; /* gas giants spin fast */

    /* Surface temperature: simplified from stellar luminosity and orbital distance */
    double flux = star->luminosity_solar / (p->orbital_radius_au * p->orbital_radius_au);
    double t_eff = 278.0 * pow(flux, 0.25); /* equilibrium temperature, Earth-normalized */
    p->surface_temp_k = t_eff;

    /* Atmosphere — depends on type */
    switch (p->type) {
        case PLANET_GAS_GIANT:
        case PLANET_ICE_GIANT:
            p->atmosphere_pressure_atm = 100.0 + rng_double(rng) * 900.0;
            break;
        case PLANET_ROCKY:
        case PLANET_DESERT:
        case PLANET_IRON:
            p->atmosphere_pressure_atm = rng_double(rng) * 2.0;
            break;
        case PLANET_SUPER_EARTH:
        case PLANET_OCEAN:
            p->atmosphere_pressure_atm = 0.5 + rng_double(rng) * 5.0;
            break;
        case PLANET_LAVA:
            p->atmosphere_pressure_atm = 0.1 + rng_double(rng) * 10.0;
            break;
        case PLANET_ICE:
        case PLANET_ROGUE:
            p->atmosphere_pressure_atm = rng_double(rng) * 0.5;
            break;
        case PLANET_CARBON:
            p->atmosphere_pressure_atm = 0.5 + rng_double(rng) * 3.0;
            break;
        default:
            p->atmosphere_pressure_atm = rng_double(rng) * 1.0;
    }

    /* Greenhouse effect: thicker atmosphere → hotter */
    if (p->atmosphere_pressure_atm > 0.1 && p->type != PLANET_GAS_GIANT && p->type != PLANET_ICE_GIANT) {
        double greenhouse = 1.0 + 0.1 * log(1.0 + p->atmosphere_pressure_atm);
        p->surface_temp_k *= greenhouse;
    }

    /* Water */
    p->water_coverage = 0.0;
    if (p->type == PLANET_OCEAN) {
        p->water_coverage = 0.6 + rng_double(rng) * 0.4;
    } else if (p->type == PLANET_SUPER_EARTH || p->type == PLANET_ROCKY) {
        if (p->surface_temp_k > 200 && p->surface_temp_k < 400 && p->atmosphere_pressure_atm > 0.01) {
            p->water_coverage = rng_double(rng) * 0.8;
        }
    }

    /* Magnetic field */
    if (p->type == PLANET_GAS_GIANT) {
        p->magnetic_field = 5.0 + rng_double(rng) * 15.0;
    } else if (p->mass_earth > 0.5 && p->rotation_period_hours < 48.0) {
        p->magnetic_field = 0.1 + rng_double(rng) * 2.0;
    } else {
        p->magnetic_field = rng_double(rng) * 0.1;
    }

    /* Habitability index — rough composite */
    p->habitability_index = 0.0;
    if (p->surface_temp_k > 200 && p->surface_temp_k < 340) {
        double temp_score = 1.0 - fabs(p->surface_temp_k - 288.0) / 100.0;
        if (temp_score < 0) temp_score = 0;
        double atm_score = (p->atmosphere_pressure_atm > 0.1 && p->atmosphere_pressure_atm < 5.0) ? 1.0 : 0.2;
        double water_score = p->water_coverage;
        double mag_score = p->magnetic_field > 0.1 ? 1.0 : 0.3;
        double mass_score = (p->mass_earth > 0.3 && p->mass_earth < 5.0) ? 1.0 : 0.2;
        p->habitability_index = (temp_score * 0.3 + atm_score * 0.2 +
                                  water_score * 0.2 + mag_score * 0.15 + mass_score * 0.15);
        if (p->habitability_index > 1.0) p->habitability_index = 1.0;
    }

    /* Rings — mostly gas giants */
    p->rings = false;
    if (p->type == PLANET_GAS_GIANT && rng_double(rng) < 0.4) p->rings = true;
    if (p->type == PLANET_ICE_GIANT && rng_double(rng) < 0.2) p->rings = true;

    /* Moons */
    if (p->type == PLANET_GAS_GIANT) {
        p->moon_count = (uint8_t)rng_range(rng, 8) + 2;
    } else if (p->type == PLANET_ICE_GIANT) {
        p->moon_count = (uint8_t)rng_range(rng, 5) + 1;
    } else if (p->mass_earth > 0.1) {
        p->moon_count = (uint8_t)rng_range(rng, 3);
    } else {
        p->moon_count = 0;
    }
    if (p->moon_count > MAX_MOONS) p->moon_count = MAX_MOONS;

    /* Survey state — nothing surveyed yet */
    memset(p->surveyed, 0, sizeof(p->surveyed));
    p->discovered_by = uid_null();
    p->discovery_tick = 0;

    /* Resources */
    generate_resources(p->resources, rng, p->type);
}

/* ---- System generation ---- */

void generate_system(system_t *sys, rng_t *rng, vec3_t galactic_pos) {
    memset(sys, 0, sizeof(*sys));

    sys->id = generate_uid(rng);
    sys->position = galactic_pos;
    sys->visited = false;
    sys->first_visit_tick = 0;

    /* Most systems have 1 star, some binaries/triples */
    double r = rng_double(rng);
    if (r < 0.70)      sys->star_count = 1;
    else if (r < 0.95) sys->star_count = 2;
    else                sys->star_count = 3;

    /* Generate stars */
    for (int i = 0; i < sys->star_count; i++) {
        /* Offset companion stars slightly */
        vec3_t star_pos = galactic_pos;
        if (i > 0) {
            star_pos.x += (rng_double(rng) - 0.5) * 0.001;
            star_pos.y += (rng_double(rng) - 0.5) * 0.001;
        }
        generate_star(&sys->stars[i], rng, star_pos);
    }

    /* Generate system name from primary star */
    memcpy(sys->name, sys->stars[0].name, MAX_NAME);

    /* Planet count: depends on star type, metallicity */
    star_t *primary = &sys->stars[0];
    int base_planets;
    if (primary->class == STAR_NEUTRON || primary->class == STAR_BLACK_HOLE) {
        base_planets = (int)rng_range(rng, 3); /* few or none */
    } else if (primary->class == STAR_O || primary->class == STAR_B) {
        base_planets = 1 + (int)rng_range(rng, 4); /* young, fewer */
    } else {
        base_planets = 2 + (int)rng_range(rng, 10); /* typical: 2-11 */
    }
    /* Higher metallicity → more planets */
    if (primary->metallicity > 0.1) base_planets += 1 + (int)rng_range(rng, 2);
    /* Multi-star → fewer stable orbits */
    if (sys->star_count > 1) base_planets = base_planets * 2 / 3;

    sys->planet_count = (uint8_t)CLAMP(base_planets, 0, MAX_PLANETS);

    /* Generate planets around primary star */
    for (int i = 0; i < sys->planet_count; i++) {
        generate_planet(&sys->planets[i], rng, i, primary);
    }
}

/* ---- Sector generation ---- */

int sector_star_count(rng_t *rng, sector_coord_t coord) {
    /* Galactic position in light-years (sector coords × sector size) */
    double sector_size_ly = 100.0; /* each sector is ~100 ly on a side */
    double gx = coord.x * sector_size_ly;
    double gy = coord.y * sector_size_ly;
    double gz = coord.z * sector_size_ly;

    /* Vertical (z) density falloff — galaxy is a thin disk */
    double z_density = exp(-(gz * gz) / (2.0 * 500.0 * 500.0)); /* scale height ~500 ly */

    /* Spiral arm density */
    double arm_density = spiral_arm_density(gx, gy);

    /* Combined: base count scaled by density.
     * A dense arm sector might have 5-15 systems.
     * A sparse halo sector might have 0-2. */
    double density = arm_density * z_density;
    int base = (int)(density * 12.0);
    int jitter = (int)rng_range(rng, (uint64_t)(base / 2 + 1));
    int count = base + jitter;

    return CLAMP(count, 0, 30); /* cap at 30 systems per sector */
}

int generate_sector(system_t *out, int max_systems,
                    uint64_t galaxy_seed, sector_coord_t coord) {
    rng_t rng;
    rng_derive(&rng, galaxy_seed, coord.x, coord.y, coord.z);

    int count = sector_star_count(&rng, coord);
    if (count > max_systems) count = max_systems;

    double sector_size_ly = 100.0;
    double base_x = coord.x * sector_size_ly;
    double base_y = coord.y * sector_size_ly;
    double base_z = coord.z * sector_size_ly;

    for (int i = 0; i < count; i++) {
        /* Random position within sector */
        vec3_t pos = {
            base_x + rng_double(&rng) * sector_size_ly,
            base_y + rng_double(&rng) * sector_size_ly,
            base_z + rng_double(&rng) * sector_size_ly,
        };
        out[i].sector = coord;
        generate_system(&out[i], &rng, pos);
    }

    return count;
}
