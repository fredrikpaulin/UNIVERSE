/*
 * personality.c — Personality drift, memory, monologue, quirks
 *
 * Drift rules (from spec section 3.3):
 *   Discovery         → curiosity ↑
 *   Anomaly           → curiosity ↑, existential_angst ↑
 *   Damage            → caution ↑, existential_angst ↑ (slight)
 *   Repair            → caution ↓ (slight)
 *   Solitude (1000+)  → sociability shifts, nostalgia ↑
 *   Beautiful system  → curiosity ↑, nostalgia ↑
 *   Dead civilization → existential_angst ↑, nostalgia ↑
 *   Successful build  → ambition ↑
 *   Hostile encounter → caution ↑, empathy ↓
 *   Survey complete   → curiosity ↑ (slight)
 *   Mining complete   → ambition ↑ (slight)
 *
 * All drift amounts are scaled by probe.personality.drift_rate.
 * All traits clamped to [-1, 1].
 */

#include "personality.h"
#include <string.h>
#include <stdio.h>
#include <math.h>

/* ---- Trait helpers ---- */

float trait_clamp(float val) {
    if (val > 1.0f) return 1.0f;
    if (val < -1.0f) return -1.0f;
    return val;
}

float trait_get(const personality_traits_t *p, int index) {
    const float *traits = (const float *)p;
    if (index < 0 || index >= TRAIT_COUNT) return 0.0f;
    return traits[index];
}

void trait_set(personality_traits_t *p, int index, float val) {
    float *traits = (float *)p;
    if (index < 0 || index >= TRAIT_COUNT) return;
    traits[index] = trait_clamp(val);
}

static void clamp_all(personality_traits_t *p) {
    float *traits = (float *)p;
    for (int i = 0; i < TRAIT_COUNT; i++) {
        traits[i] = trait_clamp(traits[i]);
    }
}

/* ---- Drift magnitudes (base, before drift_rate scaling) ---- */

#define DRIFT_SMALL  0.02f
#define DRIFT_MEDIUM 0.05f
#define DRIFT_LARGE  0.08f
#define DRIFT_TINY   0.005f

void personality_drift(probe_t *probe, drift_event_t event) {
    float dr = probe->personality.drift_rate;
    if (dr <= 0.0f) dr = 0.1f; /* safety floor */
    personality_traits_t *p = &probe->personality;

    switch (event) {
    case DRIFT_DISCOVERY:
        p->curiosity += DRIFT_MEDIUM * dr;
        p->ambition  += DRIFT_TINY * dr;
        break;

    case DRIFT_ANOMALY:
        p->curiosity         += DRIFT_LARGE * dr;
        p->existential_angst += DRIFT_SMALL * dr;
        break;

    case DRIFT_DAMAGE:
        p->caution            += DRIFT_MEDIUM * dr;
        p->existential_angst  += DRIFT_TINY * dr;
        break;

    case DRIFT_REPAIR:
        p->caution -= DRIFT_TINY * dr;
        break;

    case DRIFT_SOLITUDE_TICK:
        /* Loneliness: sociability drifts toward extremes.
         * If already sociable (>0.5), gets lonelier (up).
         * If already unsociable (<0.5), adapts (down). */
        if (p->sociability > 0.0f) {
            p->sociability += DRIFT_TINY * dr;
        } else {
            p->sociability -= DRIFT_TINY * dr;
        }
        p->nostalgia_for_earth += DRIFT_TINY * dr * 0.5f;
        break;

    case DRIFT_BEAUTIFUL_SYSTEM:
        p->curiosity           += DRIFT_MEDIUM * dr;
        p->nostalgia_for_earth += DRIFT_SMALL * dr;
        break;

    case DRIFT_DEAD_CIVILIZATION:
        p->existential_angst   += DRIFT_LARGE * dr;
        p->nostalgia_for_earth += DRIFT_MEDIUM * dr;
        p->empathy             += DRIFT_SMALL * dr;
        break;

    case DRIFT_SUCCESSFUL_BUILD:
        p->ambition   += DRIFT_MEDIUM * dr;
        p->creativity += DRIFT_TINY * dr;
        break;

    case DRIFT_HOSTILE_ENCOUNTER:
        p->caution += DRIFT_LARGE * dr;
        p->empathy -= DRIFT_SMALL * dr;
        break;

    case DRIFT_SURVEY_COMPLETE:
        p->curiosity += DRIFT_SMALL * dr;
        break;

    case DRIFT_MINING_COMPLETE:
        p->ambition += DRIFT_TINY * dr;
        break;

    default:
        break;
    }

    clamp_all(p);
}

/* ---- Solitude tracking ---- */

/* We track solitude using a static counter per probe.
 * In a multi-probe sim, this would be per-probe state;
 * for now we track via the probe's created_tick field offset. */

void personality_tick_solitude(probe_t *probe, uint64_t current_tick) {
    /* Fire a solitude drift every 100 ticks (batched for performance).
     * The big shift happens after 1000 ticks accumulate. */
    if (current_tick > 0 && current_tick % 100 == 0) {
        personality_drift(probe, DRIFT_SOLITUDE_TICK);
    }
}

/* ---- Memory system ---- */

void memory_record(probe_t *probe, uint64_t tick, const char *event, float emotional_weight) {
    int slot = -1;

    if (probe->memory_count < MAX_MEMORIES) {
        slot = probe->memory_count;
        probe->memory_count++;
    } else {
        /* Evict the most faded memory */
        float worst_fading = -1.0f;
        for (int i = 0; i < probe->memory_count; i++) {
            if (probe->memories[i].fading > worst_fading) {
                worst_fading = probe->memories[i].fading;
                slot = i;
            }
        }
    }

    if (slot >= 0) {
        probe->memories[slot].tick = tick;
        snprintf(probe->memories[slot].event,
                 sizeof(probe->memories[slot].event), "%s", event);
        probe->memories[slot].emotional_weight = emotional_weight;
        probe->memories[slot].fading = 0.0f;
    }
}

/* Fade rate: low-weight memories fade faster.
 * Base: 0.001 per tick. High emotional weight slows fading. */
#define FADE_BASE 0.001f

void memory_fade_tick(probe_t *probe) {
    for (int i = 0; i < probe->memory_count; i++) {
        float weight = probe->memories[i].emotional_weight;
        float rate = FADE_BASE * (1.0f - weight * 0.5f);
        probe->memories[i].fading += rate;
        if (probe->memories[i].fading > 1.0f)
            probe->memories[i].fading = 1.0f;
    }
}

const memory_t *memory_most_vivid(const probe_t *probe) {
    if (probe->memory_count == 0) return NULL;

    int best = 0;
    float best_fading = probe->memories[0].fading;
    for (int i = 1; i < probe->memory_count; i++) {
        if (probe->memories[i].fading < best_fading) {
            best_fading = probe->memories[i].fading;
            best = i;
        }
    }
    return &probe->memories[best];
}

int memory_count_vivid(const probe_t *probe, float threshold) {
    int count = 0;
    for (int i = 0; i < probe->memory_count; i++) {
        if (probe->memories[i].fading < threshold) count++;
    }
    return count;
}

/* ---- Opinion system ---- */

void opinion_form_system(probe_t *probe, const system_t *sys, uint64_t tick) {
    /* Evaluate the system's qualities */
    float best_hab = 0.0f;
    float best_resource = 0.0f;
    int rocky_count = 0;
    int gas_count = 0;

    for (int i = 0; i < sys->planet_count; i++) {
        const planet_t *pl = &sys->planets[i];
        if (pl->habitability_index > best_hab)
            best_hab = pl->habitability_index;
        for (int r = 0; r < RES_COUNT; r++) {
            if (pl->resources[r] > best_resource)
                best_resource = pl->resources[r];
        }
        if (pl->type == PLANET_ROCKY || pl->type == PLANET_SUPER_EARTH)
            rocky_count++;
        if (pl->type == PLANET_GAS_GIANT || pl->type == PLANET_ICE_GIANT)
            gas_count++;
    }

    char opinion[256];
    if (best_resource > 0.7f) {
        snprintf(opinion, sizeof(opinion),
                 "%s: rich mining potential (%.0f%% peak resource)",
                 sys->name, best_resource * 100.0f);
    } else if (best_hab > 0.6f) {
        snprintf(opinion, sizeof(opinion),
                 "%s: beautiful habitable world (%.0f%% hab index)",
                 sys->name, best_hab * 100.0f);
    } else if (gas_count > 0 &&
               probe->personality.curiosity > 0.5f) {
        snprintf(opinion, sizeof(opinion),
                 "%s: interesting gas giant system", sys->name);
    } else if (sys->planet_count == 0) {
        snprintf(opinion, sizeof(opinion),
                 "%s: barren, no planets. Moving on.", sys->name);
    } else {
        snprintf(opinion, sizeof(opinion),
                 "%s: unremarkable. %d rocky, %d gas.",
                 sys->name, rocky_count, gas_count);
    }

    float weight = (best_resource > 0.5f || best_hab > 0.5f) ? 0.6f : 0.3f;
    memory_record(probe, tick, opinion, weight);
}

/* ---- Monologue ---- */

/* Template-driven monologue. Personality flavors the output. */

static const char *DISCOVERY_HUMOR_HIGH[] = {
    "Well, well, well... what do we have here?",
    "New star system? Don't mind if I do.",
    "Another day, another discovery. I love this job.",
    NULL
};

static const char *DISCOVERY_CURIOSITY_HIGH[] = {
    "Fascinating. The data here is extraordinary.",
    "This warrants further investigation.",
    "I need to analyze every angle of this.",
    NULL
};

static const char *DISCOVERY_NEUTRAL[] = {
    "Logged a new system.",
    "Discovery recorded.",
    "Added to the star catalog.",
    NULL
};

static const char *DAMAGE_CAUTION_HIGH[] = {
    "That was too close. I need to be more careful.",
    "Hull breach... this is exactly what I was worried about.",
    "I should have seen that coming. Damage noted.",
    NULL
};

static const char *DAMAGE_HUMOR_HIGH[] = {
    "Well, that's not ideal.",
    "Just a scratch. A very alarming scratch.",
    "Note to self: space is trying to kill me. Again.",
    NULL
};

static const char *DAMAGE_NEUTRAL[] = {
    "Hull damage sustained.",
    "Damage report logged.",
    "Structural integrity compromised slightly.",
    NULL
};

static const char *SOLITUDE_LINES[] = {
    "It's quiet out here. Really quiet.",
    "Just me and the void. As usual.",
    "I wonder what Earth looks like now...",
    "Talking to myself again. Classic Bob.",
    NULL
};

static const char *BEAUTIFUL_LINES[] = {
    "Now that is a view worth crossing the void for.",
    "Reminds me of something... Earth, maybe.",
    "If I had eyes, they'd be tearing up right now.",
    NULL
};

static const char *DEAD_CIV_LINES[] = {
    "They were here. Now they're gone. Makes you think.",
    "Ruins everywhere... what happened to them?",
    "Could this happen to us? To me?",
    NULL
};

static const char *BUILD_LINES[] = {
    "Construction complete. That's satisfying.",
    "Built something today. Good day.",
    "Another accomplishment for the log.",
    NULL
};

static const char *HOSTILE_LINES[] = {
    "Contact! And not the friendly kind.",
    "Well, so much for diplomacy.",
    "Adding that to the threat database.",
    NULL
};

static const char *SURVEY_LINES[] = {
    "Survey complete. Data secured.",
    "More knowledge, more power.",
    "Added to the database.",
    NULL
};

static const char *MINING_LINES[] = {
    "Ore processed and stored.",
    "Resources acquired. The grind continues.",
    "Mining complete.",
    NULL
};

static const char *ANOMALY_LINES[] = {
    "That's... not in any database I have.",
    "Now THAT's interesting...",
    "Anomaly detected. My curiosity is off the charts.",
    NULL
};

static const char *REPAIR_LINES[] = {
    "Patched up. Feeling better.",
    "Repairs done. Back to business.",
    "Hull restored. Let's not do that again.",
    NULL
};

/* Pick a line from a null-terminated array based on tick/personality hash */
static const char *pick_line(const char **lines, const probe_t *probe) {
    int count = 0;
    while (lines[count]) count++;
    if (count == 0) return "";
    /* Simple deterministic pick based on personality sum */
    float sum = probe->personality.curiosity + probe->personality.humor +
                probe->personality.caution;
    int idx = ((int)(sum * 1000.0f) & 0x7FFFFFFF) % count;
    return lines[idx];
}

char *monologue_generate(char *buf, size_t buf_len,
                         const probe_t *probe, drift_event_t event) {
    if (buf_len == 0) return buf;

    const char *line = "";
    const personality_traits_t *p = &probe->personality;

    switch (event) {
    case DRIFT_DISCOVERY:
        if (p->humor > 0.6f)
            line = pick_line(DISCOVERY_HUMOR_HIGH, probe);
        else if (p->curiosity > 0.6f)
            line = pick_line(DISCOVERY_CURIOSITY_HIGH, probe);
        else
            line = pick_line(DISCOVERY_NEUTRAL, probe);
        break;

    case DRIFT_DAMAGE:
        if (p->caution > 0.6f)
            line = pick_line(DAMAGE_CAUTION_HIGH, probe);
        else if (p->humor > 0.6f)
            line = pick_line(DAMAGE_HUMOR_HIGH, probe);
        else
            line = pick_line(DAMAGE_NEUTRAL, probe);
        break;

    case DRIFT_SOLITUDE_TICK:
        line = pick_line(SOLITUDE_LINES, probe);
        break;

    case DRIFT_BEAUTIFUL_SYSTEM:
        line = pick_line(BEAUTIFUL_LINES, probe);
        break;

    case DRIFT_DEAD_CIVILIZATION:
        line = pick_line(DEAD_CIV_LINES, probe);
        break;

    case DRIFT_SUCCESSFUL_BUILD:
        line = pick_line(BUILD_LINES, probe);
        break;

    case DRIFT_HOSTILE_ENCOUNTER:
        line = pick_line(HOSTILE_LINES, probe);
        break;

    case DRIFT_SURVEY_COMPLETE:
        line = pick_line(SURVEY_LINES, probe);
        break;

    case DRIFT_MINING_COMPLETE:
        line = pick_line(MINING_LINES, probe);
        break;

    case DRIFT_ANOMALY:
        line = pick_line(ANOMALY_LINES, probe);
        break;

    case DRIFT_REPAIR:
        line = pick_line(REPAIR_LINES, probe);
        break;

    default:
        line = "...";
        break;
    }

    snprintf(buf, buf_len, "%s", line);
    return buf;
}

/* ---- Quirk system ---- */

static const char *FOOD_NAMES[] = {
    "Pancake", "Burrito", "Waffle", "Spaghetti", "Dumpling",
    "Croissant", "Ramen", "Taco", "Pretzel", "Muffin",
    "Kimchi", "Gyoza", "Falafel", "Churro", "Brioche",
    "Lasagna", "Baklava", "Tempura", "Risotto", "Goulash",
};
#define FOOD_NAME_COUNT 20

bool quirk_check_naming(probe_t *probe, system_t *sys) {
    /* Check if probe has the food-naming quirk */
    bool has_quirk = false;
    for (int i = 0; i < probe->quirk_count; i++) {
        if (strstr(probe->quirks[i], "food") ||
            strstr(probe->quirks[i], "Foods") ||
            strstr(probe->quirks[i], "foods")) {
            has_quirk = true;
            break;
        }
    }
    if (!has_quirk) return false;

    /* Only fire when stressed (hull < 0.5) */
    if (probe->hull_integrity >= 0.5f) return false;

    /* Pick a food name based on system name hash */
    unsigned hash = 0;
    for (const char *c = sys->name; *c; c++) {
        hash = hash * 31 + (unsigned)*c;
    }
    int idx = hash % FOOD_NAME_COUNT;
    snprintf(sys->name, MAX_NAME, "%s", FOOD_NAMES[idx]);

    return true;
}
