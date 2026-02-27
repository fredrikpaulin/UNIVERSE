# API Reference

Complete public API for every module, organized by header file.

## universe.h — Core Types

No functions (inline helpers only). Defines all fundamental types.

### Constants

```c
MAX_PROBES       1024    MAX_PLANETS      16      MAX_STARS        3
MAX_MOONS        10      MAX_MODULES      16      MAX_NAME         64
MAX_MEMORIES     256     MAX_GOALS        32      MAX_QUIRKS       8
MAX_QUIRK_LEN    128     MAX_RELATIONSHIPS 64     MAX_CATCHPHRASES 8
MAX_VALUES       8       MAX_EARTH_MEM    16      MAX_EARTH_MEM_LEN 256
TICKS_PER_CYCLE  365     CYCLES_PER_EPOCH 1000
```

### Key Types

- **`probe_uid_t`** — 128-bit UID (`uint64_t hi, lo`)
- **`vec3_t`** — 3D position (`double x, y, z`)
- **`sector_coord_t`** — sector grid position (`int32_t x, y, z`)
- **`personality_traits_t`** — 10 float traits + drift_rate
- **`star_t`** — star with class, mass, luminosity, temp, age, metallicity, position
- **`planet_t`** — planet with type, orbital params, atmosphere, resources, habitability
- **`system_t`** — star system with up to 3 stars and 16 planets
- **`memory_t`** — episodic memory with event text, emotional weight, fading
- **`goal_t`** — probe goal with description, priority, status
- **`relationship_t`** — inter-probe relationship with trust and disposition
- **`probe_t`** — complete probe state (~88KB): position, resources, tech, personality, memories, goals, relationships
- **`universe_t`** — simulation state (~90MB): seed, tick, probes[1024]

### Enums

- `star_class_t`: O, B, A, F, G, K, M, WHITE_DWARF, NEUTRON, BLACK_HOLE
- `planet_type_t`: GAS_GIANT, ICE_GIANT, ROCKY, SUPER_EARTH, OCEAN, LAVA, DESERT, ICE, CARBON, IRON, ROGUE
- `resource_t`: IRON, SILICON, RARE_EARTH, WATER, HYDROGEN, HELIUM3, CARBON, URANIUM, EXOTIC
- `tech_domain_t`: PROPULSION, SENSORS, MINING, CONSTRUCTION, COMPUTING, ENERGY, MATERIALS, COMMUNICATION, WEAPONS, BIOTECH
- `probe_status_t`: ACTIVE, TRAVELING, MINING, BUILDING, REPLICATING, DORMANT, DAMAGED, DESTROYED
- `event_type_t`: DISCOVERY, ANOMALY, HAZARD, ENCOUNTER, CRISIS, WONDER, MESSAGE, REPLICATION

### Inline Helpers

```c
bool uid_eq(probe_uid_t a, probe_uid_t b);
probe_uid_t uid_null(void);
bool uid_is_null(probe_uid_t id);
```

---

## rng.h — Random Number Generation

xoshiro256** PRNG with splitmix64 seeding.

```c
void     rng_seed(rng_t *rng, uint64_t seed);
uint64_t rng_next(rng_t *rng);
double   rng_double(rng_t *rng);           // [0, 1)
uint64_t rng_range(rng_t *rng, uint64_t max); // [0, max)
double   rng_gaussian(rng_t *rng);          // mean=0, stddev=1
void     rng_derive(rng_t *rng, uint64_t seed, int32_t x, int32_t y, int32_t z);
```

`rng_derive` creates a deterministic sub-RNG from a parent seed plus 3D coordinates. Used for sector generation.

---

## arena.h — Bump Allocator

```c
int    arena_init(arena_t *a, size_t capacity);
void  *arena_alloc(arena_t *a, size_t n);    // 8-byte aligned, NULL if full
void   arena_reset(arena_t *a);               // free all, keep buffer
void   arena_destroy(arena_t *a);             // free buffer
```

---

## persist.h — SQLite Persistence

```c
int  persist_open(persist_t *p, const char *path);
void persist_close(persist_t *p);
int  persist_save_meta(persist_t *p, const universe_t *u);
int  persist_load_meta(persist_t *p, universe_t *u);
int  persist_save_tick(persist_t *p, uint64_t tick);
int  persist_save_sector(persist_t *p, sector_coord_t coord, uint64_t tick,
                         const system_t *systems, int count);
int  persist_sector_exists(persist_t *p, sector_coord_t coord);
int  persist_load_sector(persist_t *p, sector_coord_t coord,
                         system_t *out, int max_systems);
```

---

## generate.h — Procedural Generation

```c
void        generate_system(system_t *sys, rng_t *rng, vec3_t galactic_pos);
int         generate_sector(system_t *out, int max_systems,
                            uint64_t galaxy_seed, sector_coord_t coord);
int         sector_star_count(rng_t *rng, sector_coord_t coord);
void        habitable_zone(double luminosity_solar, double *inner_au, double *outer_au);
probe_uid_t generate_uid(rng_t *rng);
```

---

## probe.h — Probe Actions

### Action Types

```c
ACT_NAVIGATE_TO_BODY, ACT_ENTER_ORBIT, ACT_LAND, ACT_LAUNCH,
ACT_SURVEY, ACT_MINE, ACT_WAIT, ACT_REPAIR
```

### Functions

```c
int             probe_init_bob(probe_t *probe);
action_result_t probe_execute_action(probe_t *probe, const action_t *action, system_t *sys);
void            probe_tick_energy(probe_t *probe);
int             persist_save_probe(void *persist, const probe_t *probe);
int             persist_load_probe(void *persist, probe_uid_t id, probe_t *probe);
```

---

## travel.h — Interstellar Travel

```c
travel_result_t      travel_initiate(probe_t *probe, const travel_order_t *order);
travel_tick_result_t travel_tick(probe_t *probe, rng_t *rng);
int                  travel_scan(const probe_t *probe, const system_t *systems,
                                 int system_count, scan_result_t *out, int max_results);
double               travel_lorentz_factor(double speed_c);
```

---

## agent_ipc.h — Agent Protocol

### Serialization

```c
int obs_serialize(const probe_t *probe, const system_t *sys, char *buf, int buf_size);
int action_parse(const char *json, action_t *out);
int result_serialize(const action_result_t *res, char *buf, int buf_size);
```

### Name Lookups

```c
resource_t     resource_from_name(const char *name);
const char    *resource_to_name(resource_t r);
action_type_t  action_type_from_name(const char *name);
const char    *action_type_to_name(action_type_t t);
```

### Fallback Agent

```c
action_t fallback_agent_decide(const probe_t *probe);
```

### Protocol Framing

```c
int protocol_frame(const char *msg, char *out, int out_size);
int protocol_unframe(const char *buf, int buf_len, char *out, int out_size);
```

### Agent Router

```c
void agent_router_init(agent_router_t *r);
void agent_router_destroy(agent_router_t *r);
int  agent_router_register(agent_router_t *r, probe_uid_t probe_id, int fd);
void agent_router_unregister(agent_router_t *r, probe_uid_t probe_id);
int  agent_router_lookup(const agent_router_t *r, probe_uid_t probe_id);
```

---

## render.h — Visualization Logic

### Star/Planet Display

```c
rgba_t      star_class_color(star_class_t class);
const char *star_class_name(star_class_t class);
const char *planet_type_name(planet_type_t type);
```

### View State Machine

```c
void view_state_init(view_state_t *vs);
void view_state_select_system(view_state_t *vs, probe_uid_t system_id);
void view_state_select_planet(view_state_t *vs, probe_uid_t planet_id);
void view_state_select_probe(view_state_t *vs, probe_uid_t probe_id);
void view_state_back(view_state_t *vs);
```

Views: `VIEW_GALAXY`, `VIEW_SYSTEM`, `VIEW_PROBE`

### Simulation Speed

```c
void        sim_speed_init(sim_speed_t *s);
void        sim_speed_init_target(sim_speed_t *s, double sim_years, double real_hours, int fps);
void        sim_speed_toggle_pause(sim_speed_t *s);
void        sim_speed_up(sim_speed_t *s);
void        sim_speed_down(sim_speed_t *s);
int         sim_speed_ticks_this_frame(sim_speed_t *s);
const char *sim_speed_label(const sim_speed_t *s);
```

### Camera

```c
void world_to_screen(const camera_2d_t *cam, double wx, double wy, double *sx, double *sy);
void screen_to_world(const camera_2d_t *cam, double sx, double sy, double *wx, double *wy);
void camera_zoom(camera_2d_t *cam, double factor);
```

### Hit Testing & Trails

```c
probe_uid_t hit_test_system(const system_t *systems, int count,
                            const camera_2d_t *cam,
                            double screen_x, double screen_y, double threshold_px);
void   probe_trail_init(probe_trail_t *t);
void   probe_trail_push(probe_trail_t *t, vec3_t point);
vec3_t probe_trail_get(const probe_trail_t *t, int index);
void   planet_orbital_pos(const planet_t *p, uint64_t tick, double *out_x, double *out_y);
```

---

## personality.h — Personality & Memory

### Drift

```c
void personality_drift(probe_t *probe, drift_event_t event);
void personality_tick_solitude(probe_t *probe, uint64_t current_tick);
```

Drift events: `DRIFT_DISCOVERY`, `DRIFT_ANOMALY`, `DRIFT_DAMAGE`, `DRIFT_REPAIR`, `DRIFT_SOLITUDE_TICK`, `DRIFT_BEAUTIFUL_SYSTEM`, `DRIFT_DEAD_CIVILIZATION`, `DRIFT_SUCCESSFUL_BUILD`, `DRIFT_HOSTILE_ENCOUNTER`, `DRIFT_SURVEY_COMPLETE`, `DRIFT_MINING_COMPLETE`

### Memory

```c
void            memory_record(probe_t *probe, uint64_t tick, const char *event, float emotional_weight);
void            memory_fade_tick(probe_t *probe);
const memory_t *memory_most_vivid(const probe_t *probe);
int             memory_count_vivid(const probe_t *probe, float threshold);
```

### Opinion & Monologue

```c
void  opinion_form_system(probe_t *probe, const system_t *sys, uint64_t tick);
char *monologue_generate(char *buf, size_t buf_len, const probe_t *probe, drift_event_t event);
```

### Quirks & Traits

```c
bool  quirk_check_naming(probe_t *probe, system_t *sys);
float trait_clamp(float val);
float trait_get(const personality_traits_t *p, int index);
void  trait_set(personality_traits_t *p, int index, float val);
```

---

## replicate.h — Self-Replication

### Constants

```c
REPL_COST_IRON 200000.0  REPL_COST_SILICON 100000.0  REPL_COST_RARE_EARTH 50000.0
REPL_COST_CARBON 50000.0 REPL_COST_WATER 50000.0     REPL_COST_URANIUM 25000.0
REPL_COST_HYDROGEN 15000.0  REPL_COST_HELIUM3 5000.0  REPL_COST_EXOTIC 5000.0
REPL_TOTAL_KG 500000.0   REPL_BASE_TICKS 200   REPL_CONSCIOUSNESS_FORK_PCT 0.80
```

### Replication Lifecycle

```c
int repl_check_resources(const probe_t *parent);
int repl_begin(probe_t *parent, replication_state_t *state);
int repl_tick(probe_t *parent, replication_state_t *state);       // 0=progress, 1=complete
int repl_finalize(probe_t *parent, probe_t *child,
                  replication_state_t *state, rng_t *rng);
```

### Mutation & Inheritance

```c
void personality_mutate(const personality_traits_t *parent,
                        personality_traits_t *child, rng_t *rng);
void earth_memory_degrade(probe_t *child);
void quirk_inherit(const probe_t *parent, probe_t *child, rng_t *rng);
void name_generate_child(char *name, size_t len, const char *parent_name, rng_t *rng);
```

### Lineage

```c
void lineage_record(lineage_tree_t *tree, probe_uid_t parent_id,
                    probe_uid_t child_id, uint64_t tick, uint32_t generation);
int  lineage_children(const lineage_tree_t *tree, probe_uid_t parent_id,
                      probe_uid_t *out, int max_out);
```

---

## communicate.h — Communication

### Constants

```c
LIGHT_SPEED_LY_PER_TICK (1.0/365.0)  COMM_BASE_RANGE_LY 5.0
COMM_RANGE_PER_LEVEL 5.0              RELAY_RANGE_LY 20.0
COMM_ENERGY_TARGETED 1000.0           COMM_ENERGY_BROADCAST 10000.0
```

### Core API

```c
void     comm_init(comm_system_t *cs);
double   comm_range(const probe_t *probe);
uint64_t comm_light_delay(vec3_t from, vec3_t to);
int      comm_send_targeted(comm_system_t *cs, probe_t *sender,
                            probe_uid_t target_id, vec3_t target_pos,
                            const char *content, uint64_t current_tick);
int      comm_send_broadcast(comm_system_t *cs, probe_t *sender,
                             const probe_t *all_probes, int probe_count,
                             const char *content, uint64_t current_tick);
double   comm_check_reachable(const comm_system_t *cs, const probe_t *sender,
                              vec3_t target_pos);
int      comm_tick_deliver(comm_system_t *cs, uint64_t current_tick);
int      comm_get_inbox(const comm_system_t *cs, probe_uid_t probe_id,
                        message_t *out, int max_out);
```

### Beacons

```c
int comm_place_beacon(comm_system_t *cs, const probe_t *owner,
                      probe_uid_t system_id, const char *message, uint64_t current_tick);
int comm_detect_beacons(const comm_system_t *cs, probe_uid_t system_id,
                        beacon_t *out, int max_out);
int comm_deactivate_beacon(comm_system_t *cs, probe_uid_t owner_id,
                           probe_uid_t system_id);
```

### Relay Satellites

```c
int    comm_build_relay(comm_system_t *cs, const probe_t *owner,
                        probe_uid_t system_id, uint64_t current_tick);
double comm_relay_path_distance(const comm_system_t *cs,
                                vec3_t from, vec3_t to, double direct_range);
```

---

## events.h — Events & Encounters

### Frequencies

```c
FREQ_DISCOVERY 0.005   FREQ_ANOMALY 0.001   FREQ_HAZARD 0.002
FREQ_ENCOUNTER 0.0002  FREQ_CRISIS 0.00005  FREQ_WONDER 0.0003
```

### Core API

```c
void events_init(event_system_t *es);
int  events_tick_probe(event_system_t *es, probe_t *probe,
                       const system_t *current_system, uint64_t tick, rng_t *rng);
int  events_generate(event_system_t *es, probe_t *probe,
                     event_type_t type, int subtype,
                     const system_t *sys, uint64_t tick, rng_t *rng);
```

### Hazard Effects

```c
float hazard_solar_flare(probe_t *probe, float severity);   // hull damage, mitigated by materials tech
float hazard_asteroid(probe_t *probe, float severity);       // direct hull damage
float hazard_radiation(probe_t *probe, float severity);      // compute capacity damage
```

### Alien Life

```c
int alien_check_planet(const planet_t *planet, rng_t *rng);
int alien_generate_civ(civilization_t *civ, const planet_t *planet,
                       probe_uid_t discovered_by, uint64_t tick, rng_t *rng);
```

### Queries

```c
int                  events_get_for_probe(const event_system_t *es, probe_uid_t probe_id,
                                          sim_event_t *out, int max_out);
int                  events_get_anomalies(const event_system_t *es, probe_uid_t system_id,
                                          anomaly_t *out, int max_out);
const civilization_t *events_get_civ(const event_system_t *es, probe_uid_t planet_id);
bool                  events_deterministic_check(uint64_t seed, int tick_count,
                                                 event_type_t *out_types, int *out_count, int max_out);
```

---

## society.h — Probe Society

### Constants

```c
TRUST_TRADE_POSITIVE 0.05    TRUST_SHARED_DISCOVERY 0.03   TRUST_TECH_SHARE 0.08
TRUST_COLLAB_BUILD 0.06      TRUST_CLAIM_VIOLATION -0.10   TRUST_DISAGREEMENT -0.05
TECH_SHARE_DISCOUNT 0.4
```

### Structure Types

`STRUCT_MINING_STATION`, `STRUCT_RELAY_SATELLITE`, `STRUCT_OBSERVATORY`, `STRUCT_HABITAT`, `STRUCT_SHIPYARD`, `STRUCT_FACTORY`

### Core API

```c
void                   society_init(society_t *soc);
const structure_spec_t *structure_get_spec(structure_type_t type);
```

### Relationships

```c
void    society_update_trust(probe_t *a, probe_t *b, float delta);
float   society_get_trust(const probe_t *a, probe_uid_t b_id);
uint8_t society_get_disposition(const probe_t *a, probe_uid_t b_id);
```

### Trading

```c
int society_trade_send(society_t *soc, probe_t *sender, probe_t *receiver,
                       resource_t resource, double amount,
                       bool same_system, uint64_t current_tick);
int society_trade_tick(society_t *soc, probe_t *probes, int probe_count,
                       uint64_t current_tick);
```

### Territory

```c
int         society_claim_system(society_t *soc, probe_uid_t claimer_id,
                                 probe_uid_t system_id, uint64_t tick);
probe_uid_t society_get_claim(const society_t *soc, probe_uid_t system_id);
int         society_revoke_claim(society_t *soc, probe_uid_t claimer_id,
                                 probe_uid_t system_id);
bool        society_is_claimed_by_other(const society_t *soc, probe_uid_t system_id,
                                        probe_uid_t probe_id);
```

### Construction

```c
int   society_build_start(society_t *soc, probe_t *builder,
                          structure_type_t type, probe_uid_t system_id,
                          uint64_t current_tick, rng_t *rng);
int   society_build_collaborate(society_t *soc, int structure_idx, probe_t *collaborator);
int   society_build_tick(society_t *soc, uint64_t current_tick);
float society_build_speed_mult(int builder_count);
```

### Voting

```c
int society_propose(society_t *soc, probe_uid_t proposer_id,
                    const char *text, uint64_t current_tick, uint64_t deadline_tick);
int society_vote(society_t *soc, int proposal_idx,
                 probe_uid_t voter_id, bool in_favor, uint64_t tick);
int society_resolve_votes(society_t *soc, uint64_t current_tick);
```

### Tech Sharing

```c
int      society_share_tech(probe_t *sender, probe_t *receiver, tech_domain_t domain);
uint32_t society_shared_research_ticks(uint32_t normal_ticks);
```

---

## agent_llm.h — LLM Integration

### Prompt Building

```c
int llm_build_system_prompt(const probe_t *probe, char *buf, int buf_size);
int llm_build_observation(const probe_t *probe, const system_t *sys,
                          const char *recent_events, uint64_t tick,
                          char *buf, int buf_size);
int llm_build_memory_context(const probe_t *probe, const char *rolling_summary,
                             int max_memories, char *buf, int buf_size);
int llm_build_relationship_context(const probe_t *probe, char *buf, int buf_size);
```

### Response Parsing

```c
int llm_parse_response(const char *response, action_t *actions, int max_actions,
                       char *monologue, int monologue_size);
```

### Context Management

```c
void        llm_context_init(llm_context_t *ctx, int summary_interval);
void        llm_context_append_event(llm_context_t *ctx, const char *event_desc);
const char *llm_context_get_summary(const llm_context_t *ctx);
```

### Cost Tracking

```c
void   llm_cost_init(llm_cost_tracker_t *ct, double input_rate, double output_rate);
void   llm_cost_record(llm_cost_tracker_t *ct, int input_tokens, int output_tokens);
double llm_cost_avg_per_call(const llm_cost_tracker_t *ct);
double llm_cost_avg_tokens(const llm_cost_tracker_t *ct);
```

### Deliberation

```c
void llm_delib_init(llm_deliberation_t *d, int interval);
bool llm_delib_should_call(const llm_deliberation_t *d, uint64_t current_tick);
void llm_delib_record(llm_deliberation_t *d, uint64_t tick);
void llm_delib_force(llm_deliberation_t *d);
```

### Decision Logging

```c
void llm_log_init(llm_decision_log_t *log);
void llm_log_record(llm_decision_log_t *log, uint64_t tick,
                    probe_uid_t probe_id, const action_t *action,
                    const char *monologue, int input_tokens, int output_tokens);
int  llm_log_get_for_probe(const llm_decision_log_t *log, probe_uid_t probe_id,
                           llm_decision_log_entry_t *out, int max_out);
```

### Personality Flavor

```c
int llm_personality_flavor(const personality_traits_t *p, char *buf, int buf_size);
```

---

## scenario.h — Scenario Framework

### Event Injection

```c
void inject_init(injection_queue_t *q);
int  inject_event(injection_queue_t *q, event_type_t type, int subtype,
                  const char *description, float severity, probe_uid_t target_probe_id);
int  inject_parse_json(injection_queue_t *q, const char *json);
int  inject_flush(injection_queue_t *q, event_system_t *es,
                  probe_t *probes, int probe_count,
                  const system_t *sys, uint64_t tick, rng_t *rng);
```

### Metrics

```c
void                    metrics_init(metrics_system_t *ms, int sample_interval);
void                    metrics_record(metrics_system_t *ms, const universe_t *uni,
                                       const event_system_t *es, uint64_t tick);
const metrics_snapshot_t *metrics_latest(const metrics_system_t *ms);
const metrics_snapshot_t *metrics_at(const metrics_system_t *ms, int index);
double                   metrics_avg_tech(const universe_t *uni);
float                    metrics_avg_trust(const universe_t *uni);
uint32_t                 metrics_systems_explored(const universe_t *uni);
```

### Snapshots

```c
void snapshot_take(snapshot_t *snap, const universe_t *uni, const char *tag);
int  snapshot_restore(const snapshot_t *snap, universe_t *uni);
bool snapshot_matches(const snapshot_t *a, const snapshot_t *b);
```

### Forking

```c
int universe_fork(const snapshot_t *snap, universe_t *forked, uint64_t new_seed);
```

### Configuration

```c
void        config_init(config_t *cfg);
int         config_parse_json(config_t *cfg, const char *json);
const char *config_get(const config_t *cfg, const char *key);
double      config_get_double(const config_t *cfg, const char *key, double default_val);
int         config_set(config_t *cfg, const char *key, const char *value);
```

### Replay

```c
void replay_init(replay_t *rep, const event_system_t *es,
                 uint64_t from_tick, uint64_t to_tick);
int  replay_step(replay_t *rep, sim_event_t *out, int max_out);
bool replay_done(const replay_t *rep);
```
