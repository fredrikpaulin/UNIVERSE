# Architecture

## Overview

Project UNIVERSE is a layered simulation built in pure C11. The architecture follows a strict bottom-up dependency order: each module depends only on modules below it, never above. There are no circular dependencies.

```
┌─────────────────────────────────────────────────────────────┐
│                    scenario.c (Phase 12)                    │
│         injection, metrics, snapshots, config, replay       │
├─────────────────────────────────────────────────────────────┤
│   agent_llm.c (11)  │  society.c (10)  │  events.c (9)     │
│   LLM prompts,      │  trade, claims,  │  hazards, aliens, │
│   parsing, cost      │  voting, tech    │  personality tie  │
├─────────────────────────────────────────────────────────────┤
│   communicate.c (8)  │  replicate.c (7)  │  personality.c (6)│
│   messages, beacons  │  child probes,   │  drift, memory,   │
│   relay Dijkstra     │  mutation,lineage│  monologue,quirks  │
├─────────────────────────────────────────────────────────────┤
│   render.c (5)       │  agent_ipc.c (4) │  travel.c (3)     │
│   view state,camera  │  JSON protocol,  │  interstellar     │
│   speed control      │  serialization   │  movement,sensors  │
├─────────────────────────────────────────────────────────────┤
│                      probe.c (Phase 2)                      │
│           actions, state management, persistence            │
├─────────────────────────────────────────────────────────────┤
│                    generate.c (Phase 1)                     │
│          procedural stars, planets, resources               │
├─────────────────────────────────────────────────────────────┤
│   universe.h    │  rng.h/c     │  arena.h/c   │ persist.h/c│
│   core types    │  xoshiro256  │  bump alloc   │ SQLite     │
└─────────────────────────────────────────────────────────────┘
```

## Core Types (universe.h)

Everything revolves around a handful of structs defined in `universe.h`. This is the single source of truth for data layout.

**`universe_t`** is the top-level simulation state. It holds the seed, tick counter, and an array of up to 1,024 probes. At ~90MB due to the probe array, it lives on the heap or as a static global — never on the stack.

**`probe_t`** (~88KB each) is the richest struct. A probe carries its position, resources, tech levels, personality traits, quirks, earth memories, episodic memories, goals, and relationships. This is intentional: a probe is a complete entity that can be serialized, snapshotted, or forked independently.

**`system_t`** contains up to 3 stars and 16 planets with full orbital parameters, resources, and habitability data. Systems are generated on demand from the galaxy seed.

**`probe_uid_t`** is a 128-bit unique identifier (two `uint64_t` values). Used for probes, systems, planets, and anything that needs a stable identity. Generated from the RNG with `generate_uid()`.

## Data Flow

The simulation loop processes one tick at a time. Each tick:

1. **Energy tick** — fusion reactor converts fuel to energy (`probe_tick_energy`)
2. **Travel tick** — probes in transit consume fuel and check for arrival (`travel_tick`)
3. **Event generation** — roll for discoveries, hazards, anomalies per probe (`events_tick_probe`)
4. **Agent decision** — each probe chooses an action via fallback agent, IPC agent, or LLM
5. **Action execution** — validate and apply the action (`probe_execute_action`)
6. **Communication** — deliver messages whose arrival time has been reached (`comm_tick_deliver`)
7. **Society tick** — process trades, construction, vote resolution
8. **Personality tick** — memory fading, solitude tracking
9. **Metrics** — record snapshot if aligned with sample interval
10. **Persistence** — save state to SQLite periodically

## Module Responsibilities

### Foundation Layer

**`rng.c`** — xoshiro256** PRNG. Seeded from a single 64-bit value via splitmix64. Provides `rng_double()`, `rng_range()`, `rng_gaussian()`, and `rng_derive()` for generating sector-specific sub-RNGs from coordinates. Platform-independent: same seed gives same sequence everywhere.

**`arena.c`** — Simple bump allocator. Used for per-tick scratch allocations that get reset each frame. Avoids malloc/free churn.

**`persist.c`** — SQLite wrapper. Saves universe metadata, sector data, and probe state. Uses a simple schema with blobs for large structs. `persist_save_sector` / `persist_load_sector` handles lazy generation caching.

### Generation (Phase 1)

**`generate.c`** — Procedural universe creation. `generate_sector()` derives an RNG from the galaxy seed plus sector coordinates, then generates star systems. Stars follow real Hertzsprung-Russell distribution (76.5% M-class, 0.00003% O-class). Planets get orbital parameters, atmospheres, resources, and habitability scores. `habitable_zone()` computes Goldilocks boundaries from stellar luminosity.

### Probe & Actions (Phase 2)

**`probe.c`** — Probe lifecycle and action execution. `probe_init_bob()` creates the original probe with default stats. `probe_execute_action()` is the core dispatcher — validates action legality against current state (can't mine in orbit, can't land without orbiting first, etc.) and mutates both probe and system state. Actions: navigate, orbit, land, launch, survey (5 progressive levels), mine, wait, repair.

### Travel (Phase 3)

**`travel.c`** — Interstellar movement. `travel_initiate()` sets heading and estimated arrival. `travel_tick()` consumes fuel proportional to speed, checks for arrival, and handles fuel exhaustion. `travel_scan()` returns nearby systems within sensor range, sorted by distance. `travel_lorentz_factor()` computes relativistic time dilation.

### Agent IPC (Phase 4)

**`agent_ipc.c`** — Communication protocol between the C simulation and external agents. Observations and actions are serialized as newline-delimited JSON. `obs_serialize()` packs probe state + system context. `action_parse()` decodes action strings. `agent_router_t` manages socket file descriptors per probe. A `fallback_agent_decide()` provides simple survive-logic for unconnected probes.

### Visualization (Phase 5)

**`render.c`** — Pure logic layer for the visualization (no Raylib dependency). Manages the view state machine (galaxy → system → probe), 2D camera with zoom/pan, simulation speed control with fractional tick accumulation, star/planet color mapping, hit testing for click-to-select, and probe trail ring buffers. The actual Raylib draw calls live in `render_raylib.c`, compiled only with `-DUSE_RAYLIB`.

### Personality (Phase 6)

**`personality.c`** — The heart of probe individuality. `personality_drift()` adjusts traits based on events: finding a beautiful system increases curiosity, taking damage increases caution, long solitude increases existential angst. `memory_record()` logs episodic memories with emotional weight. Memories fade over time via `memory_fade_tick()`. `monologue_generate()` produces inner thoughts based on personality + event. The quirk system gives probes idiosyncratic behaviors (naming systems after foods when stressed, etc.).

### Replication (Phase 7)

**`replicate.c`** — Von Neumann self-replication. Costs 500,000 kg of resources across 9 types. Takes 200 base ticks with consciousness forking at 80% completion. `personality_mutate()` applies gaussian noise to the parent's traits. `earth_memory_degrade()` reduces fidelity per generation — by generation 3-4, earth memories are fragments. `quirk_inherit()` has a 70% keep / 10% mutate / 20% drop rule. `name_generate_child()` creates variant names. `lineage_tree_t` tracks the full family tree.

### Communication (Phase 8)

**`communicate.c`** — Light-speed constrained messaging. Messages travel at 1 ly/year (1 tick = 1 day, so 365 ticks per light-year). `comm_send_targeted()` queues a message with computed arrival tick. `comm_send_broadcast()` sends to all probes in direct range. `comm_relay_path_distance()` uses Dijkstra's algorithm to find the shortest path through relay satellite chains. Beacons are passive markers probes can detect when entering a system.

### Events (Phase 9)

**`events.c`** — Stochastic event engine. Each tick, probes roll against per-type frequencies (discovery 0.5%, hazard 0.2%, anomaly 0.1%, etc.). Hazards cause real damage: solar flares hit hull (mitigated by materials tech), asteroids hit hull directly, radiation damages compute capacity. The alien life system uses a probability chain: ~0.01% base chance on habitable planets, weighted from 40% microbial down to 0.5% transcended. Extinct civilizations leave artifacts. Events integrate with personality drift and memory recording.

### Society (Phase 10)

**`society.c`** — Emergent probe civilization. Relationship trust is updated by interactions (trade +0.05, tech share +0.08, claim violation -0.10). Resource trading is instant within a system, 100-tick delay across systems. Territory claims create a property system with violation detection. Shared construction uses a Dijkstra speed multiplier (up to 4 collaborators, 1 + 0.6*(n-1) speedup). Voting resolves proposals by majority after a deadline. Tech sharing lets advanced probes bootstrap newer ones at 40% of normal research cost.

### LLM Agent (Phase 11)

**`agent_llm.c`** — Bridge between probes and large language models. `llm_build_system_prompt()` constructs a personality-rich system prompt with quirks, earth memories, and goals. `llm_build_observation()` formats the current game state. `llm_parse_response()` uses a hand-rolled JSON parser (no libraries) to extract actions and monologue from the LLM response. Context management uses rolling summaries that auto-compress when they exceed the interval. Cost tracking monitors token usage and USD spend. Deliberation throttling ensures the LLM is only called every N ticks.

### Scenario Framework (Phase 12)

**`scenario.c`** — Tools for experimentation. The injection queue lets you script events into the simulation. The metrics system samples simulation health at configurable intervals. Snapshot/rollback captures and restores the full universe state (all 1,024 probe slots). Universe forking clones a snapshot with a new seed for parallel experimentation. The configuration system is a JSON-parseable key-value store for runtime tuning. Replay extracts events from a tick range for historical playback.

## Memory Model

The simulation is designed around large static allocations rather than dynamic memory. `universe_t` is ~90MB (1,024 probes × 88KB each). `snapshot_t` is similarly sized. These must be allocated statically or on the heap. The arena allocator handles per-tick scratch needs. SQLite handles all disk I/O.

This approach trades memory for simplicity: no malloc/free lifecycle to manage, no pointer invalidation, no fragmentation. The tradeoff is that `MAX_PROBES` is a hard ceiling.

## Determinism

Given the same seed, the simulation produces identical results on any platform. This is achieved through:

- Platform-independent RNG (xoshiro256** with splitmix64 seeding)
- No floating-point-order-dependent operations in generation
- Sector RNGs derived from coordinates: `rng_derive(galaxy_seed, x, y, z)`
- All randomness flows through the `rng_t` state

This makes the simulation replayable and forkable: snapshot at tick N, restore, and get the exact same future.
