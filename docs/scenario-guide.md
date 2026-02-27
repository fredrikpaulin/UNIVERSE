# Scenario Guide

The scenario framework (Phase 12) provides tools for experimentation: injecting events, tracking metrics, saving/restoring state, forking alternate timelines, tuning parameters, and replaying history.

## Event Injection

Inject scripted events into the simulation without waiting for the RNG to produce them naturally.

### Programmatic Injection

```c
injection_queue_t q;
inject_init(&q);

// Queue a crisis event targeting all probes
inject_event(&q, EVT_CRISIS, CRISIS_SYSTEM_FAILURE,
             "alien fleet detected", 0.9f, uid_null());

// Queue a hazard targeting a specific probe
probe_uid_t target = {0, 1};  // Bob's ID
inject_event(&q, EVT_HAZARD, HAZ_SOLAR_FLARE,
             "massive coronal ejection", 0.8f, target);

// Flush into the event system on the next tick
int count = inject_flush(&q, &event_system, probes, probe_count,
                         &current_system, current_tick, &rng);
```

When `target_probe_id` is `uid_null()`, the event fires for every probe. When set, only the matching probe is affected.

### JSON Injection

For external tooling or scripting:

```c
const char *json = "{\"type\":\"hazard\",\"subtype\":0,"
                   "\"description\":\"Massive solar storm\","
                   "\"severity\":0.85}";

inject_parse_json(&q, json);
```

Supported type strings: `"discovery"`, `"hazard"`, `"anomaly"`, `"wonder"`, `"crisis"`, `"encounter"`.

### Event Types and Subtypes

| Type | Subtypes | Effects |
|------|----------|---------|
| Discovery | mineral_deposit, geological_formation, impact_crater, underground_water | Memory + curiosity drift |
| Hazard | solar_flare (hull), asteroid_collision (hull), radiation_burst (compute) | Real damage to probe |
| Anomaly | unexplained_signal, energy_reading, artifact | Persistent anomaly marker |
| Wonder | binary_sunset, supernova_visible, pulsar_beam, nebula_glow | Positive personality drift |
| Crisis | system_failure, resource_contamination, existential_threat | Hull + resource damage |
| Encounter | alien contact | May generate civilization |

## Metrics

Track simulation health over time with periodic snapshots.

```c
metrics_system_t metrics;
metrics_init(&metrics, 100);  // sample every 100 ticks

// In your tick loop:
metrics_record(&metrics, &universe, &event_system, current_tick);

// Query
const metrics_snapshot_t *latest = metrics_latest(&metrics);
printf("Tick %lu: %u probes, avg tech %.1f, avg trust %.2f\n",
       latest->tick, latest->probes_spawned,
       latest->avg_tech_level, latest->avg_trust);
```

### Available Metrics

Each `metrics_snapshot_t` captures:

- `tick` — when this snapshot was taken
- `systems_explored` — count of visited systems
- `probes_spawned` — total probes alive
- `total_resources_mined` / `total_resources_spent`
- `longest_survival_ticks` — longest-lived probe
- `avg_tech_level` — mean tech level across all active probes
- `total_discoveries` / `total_hazards_survived` / `total_civs_found`
- `avg_trust` — mean inter-probe trust
- `structures_built` — completed structures

### Standalone Metrics

You can also compute metrics without recording a snapshot:

```c
double tech = metrics_avg_tech(&universe);       // live avg tech
float trust = metrics_avg_trust(&universe);       // live avg trust
uint32_t explored = metrics_systems_explored(&universe);
```

## Snapshots & Rollback

Capture the full universe state and restore it later.

```c
// Static allocation required — snapshot_t is ~90MB
static snapshot_t checkpoint;

// Save
snapshot_take(&checkpoint, &universe, "before_experiment");

// ... run simulation for 1000 ticks ...

// Restore to saved state
snapshot_restore(&checkpoint, &universe);  // universe is back to the saved state

// Verify
static snapshot_t verify;
snapshot_take(&verify, &universe, "after_restore");
assert(snapshot_matches(&checkpoint, &verify));  // identical
```

`snapshot_restore` returns -1 if the snapshot is invalid (the `valid` flag is false).

### What Gets Saved

A snapshot captures: tick, seed, probe_count, and the full probe array (all 1,024 slots). This is the complete mutable state of the universe. Event logs, metrics, and communication queues are not snapshotted — they're considered derived/observational data.

## Universe Forking

Clone a snapshot into a parallel universe with a different RNG seed.

```c
static snapshot_t fork_point;
snapshot_take(&fork_point, &universe, "fork_at_1000");

static universe_t alt_universe;
universe_fork(&fork_point, &alt_universe, 12345);

// alt_universe has the same probes at the same tick,
// but seed 12345 means different future RNG rolls
```

Use cases: A/B testing event frequencies, comparing outcomes with different starting configurations, exploring alternate histories from a critical moment.

## Configuration

Tune simulation parameters at runtime without recompiling.

```c
config_t cfg;
config_init(&cfg);

// Set individual values
config_set(&cfg, "event_freq_discovery", "0.01");
config_set(&cfg, "mutation_rate", "0.15");

// Or parse from JSON
config_parse_json(&cfg, "{\"tick_rate\":60,\"event_freq_discovery\":0.008}");

// Read values
double freq = config_get_double(&cfg, "event_freq_discovery", 0.005);
const char *raw = config_get(&cfg, "mutation_rate");
```

`config_get_double()` returns the default value if the key doesn't exist, so you can use config lookups inline with fallback defaults throughout the codebase.

### Suggested Config Keys

These keys map to existing simulation constants:

| Key | Default | Description |
|-----|---------|-------------|
| `event_freq_discovery` | 0.005 | Per-tick discovery probability |
| `event_freq_hazard` | 0.002 | Per-tick hazard probability |
| `event_freq_anomaly` | 0.001 | Per-tick anomaly probability |
| `event_freq_encounter` | 0.0002 | Per-tick encounter probability |
| `event_freq_crisis` | 0.00005 | Per-tick crisis probability |
| `mutation_rate` | 0.1 | Personality mutation gaussian stddev |
| `repl_base_ticks` | 200 | Base replication duration |
| `comm_base_range_ly` | 5.0 | Base communication range |
| `comm_range_per_level` | 5.0 | Extra range per comm tech level |
| `relay_range_ly` | 20.0 | Relay satellite effective range |
| `deliberation_interval` | 10 | LLM call frequency (ticks) |

## Replay

Step through historical events tick-by-tick.

```c
replay_t replay;
replay_init(&replay, &event_system, 500, 1000);  // events from tick 500-1000

printf("Replaying %d events\n", replay.event_count);

sim_event_t events[16];
while (!replay_done(&replay)) {
    int n = replay_step(&replay, events, 16);
    for (int i = 0; i < n; i++) {
        printf("Tick %lu: [%s] %s (severity %.2f)\n",
               events[i].tick,
               event_type_name(events[i].type),
               events[i].description,
               events[i].severity);
    }
}
```

`replay_init()` copies events in the specified tick range from the event log. `replay_step()` advances one tick and returns events for that tick. The replay is non-destructive — it doesn't affect the live simulation.
