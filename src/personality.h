#ifndef PERSONALITY_H
#define PERSONALITY_H

#include "universe.h"

/* ---- Drift event types ---- */

typedef enum {
    DRIFT_DISCOVERY,         /* found new system/planet */
    DRIFT_ANOMALY,           /* found anomaly */
    DRIFT_DAMAGE,            /* took hull damage */
    DRIFT_REPAIR,            /* repaired hull */
    DRIFT_SOLITUDE_TICK,     /* one more tick alone */
    DRIFT_BEAUTIFUL_SYSTEM,  /* high habitability system */
    DRIFT_DEAD_CIVILIZATION, /* found dead civ ruins */
    DRIFT_SUCCESSFUL_BUILD,  /* built/replicated something */
    DRIFT_HOSTILE_ENCOUNTER, /* hostile aliens */
    DRIFT_SURVEY_COMPLETE,   /* finished surveying */
    DRIFT_MINING_COMPLETE,   /* mined resources */
    DRIFT_TYPE_COUNT
} drift_event_t;

/* ---- Personality drift ---- */

/* Apply a single drift event to a probe's personality.
 * Adjusts traits based on event type and current personality.
 * All traits clamped to [-1, 1]. */
void personality_drift(probe_t *probe, drift_event_t event);

/* Apply solitude tracking: call once per tick.
 * Internally tracks consecutive solo ticks and fires
 * DRIFT_SOLITUDE_TICK periodically. */
void personality_tick_solitude(probe_t *probe, uint64_t current_tick);

/* ---- Memory system ---- */

/* Record a new episodic memory. Oldest faded memory evicted if full. */
void memory_record(probe_t *probe, uint64_t tick, const char *event, float emotional_weight);

/* Fade all memories by one tick's worth. */
void memory_fade_tick(probe_t *probe);

/* Get the most vivid (lowest fading) memory, or NULL if none. */
const memory_t *memory_most_vivid(const probe_t *probe);

/* Get count of memories with fading < threshold. */
int memory_count_vivid(const probe_t *probe, float threshold);

/* ---- Opinion system ---- */

/* Form an opinion about a system after surveying. Stored as a memory. */
void opinion_form_system(probe_t *probe, const system_t *sys, uint64_t tick);

/* ---- Monologue ---- */

/* Generate an inner monologue line based on recent event and personality.
 * Writes to buf (up to buf_len). Returns buf. */
char *monologue_generate(char *buf, size_t buf_len,
                         const probe_t *probe, drift_event_t event);

/* ---- Quirk system ---- */

/* Check and fire quirks. Returns true if a quirk fired.
 * If the "names systems after foods when stressed" quirk fires,
 * it overwrites sys->name. */
bool quirk_check_naming(probe_t *probe, system_t *sys);

/* ---- Trait helpers ---- */

/* Clamp a single trait to [-1, 1]. */
float trait_clamp(float val);

/* Get a trait by index (0=curiosity, 1=caution, ..., 9=nostalgia). */
float trait_get(const personality_traits_t *p, int index);

/* Set a trait by index. Clamped automatically. */
void trait_set(personality_traits_t *p, int index, float val);

/* Number of personality traits. */
#define TRAIT_COUNT 10

#endif /* PERSONALITY_H */
