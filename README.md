# Project UNIVERSE

A persistent, procedurally generated universe simulation with sentient AI Von Neumann probes. Inspired by Dennis E. Taylor's Bobiverse series.

Earth is dying. As a last act, humanity launches a Von Neumann probe carrying a digitized human consciousness into the void. The probe wakes up alone in an unfamiliar star system with one directive: *survive, replicate, explore*.

The simulation is three things at once: a reasonably accurate procedural universe with real astrophysics driving generation, a sandbox for sentient AI agents that develop personality and form societies, and a testbed for agentic AI systems making real decisions with real consequences.

Think of it as a fishtank — something you leave running and check in on. "What did the probes get up to while I was gone?"

## Quick Start

**Prerequisites:** GCC (C11), SQLite3 development libraries, Make. Optional: Raylib (for visual mode), Python 3 + Anthropic API key (for LLM agent).

```bash
# Build and run tests
make test

# Run headless simulation
make
LD_LIBRARY_PATH=. ./universe

# Run with Raylib visualization (requires Raylib)
make visual
LD_LIBRARY_PATH=. ./universe_visual
```

## Architecture at a Glance

The codebase is pure C11 with minimal dependencies: SQLite for persistence, xoshiro256** for deterministic RNG, and optionally Raylib for visualization. No other libraries.

```
src/
  universe.h      Core types: probes, stars, planets, systems
  rng.h/c         Seeded PRNG (xoshiro256**)
  arena.h/c       Bump allocator for scratch memory
  persist.h/c     SQLite persistence layer
  generate.h/c    Procedural galaxy generation
  probe.h/c       Probe actions and state management
  travel.h/c      Interstellar travel and sensors
  agent_ipc.h/c   Agent protocol (JSON over Unix sockets)
  render.h/c      View state, camera, speed control
  personality.h/c Personality drift, memory, monologue, quirks
  replicate.h/c   Self-replication with personality mutation
  communicate.h/c Light-speed messaging, beacons, relay satellites
  events.h/c      Event engine, hazards, alien civilizations
  society.h/c     Relationships, trade, territory, voting, tech sharing
  agent_llm.h/c   LLM prompt building, response parsing, cost tracking
  scenario.h/c    Event injection, metrics, snapshots, config, replay

tools/
  test_*.c        Test suites for each phase (1,515 tests total)

agents/
  agent_llm.py    Python LLM agent (Anthropic Claude API)

vendor/
  sqlite3.h/c     Bundled SQLite
  raylib/          Raylib headers and prebuilt libraries
```

## Test Suite

1,515 tests across 12 phases, all passing:

| Phase | Module | Tests | Description |
|-------|--------|-------|-------------|
| 1 | generate | 475 | Procedural galaxy, stars, planets, resources |
| 2 | probe | 170 | Probe actions: survey, mine, repair, navigate |
| 3 | travel | 55 | Interstellar travel, fuel, sensors, Lorentz factor |
| 4 | agent_ipc | 113 | JSON protocol, observation serialization, routing |
| 5 | render | 132 | View state, camera, speed control, hit testing |
| 6 | personality | 76 | Drift, memory fading, monologue, quirks |
| 7 | replicate | 168 | Multi-tick construction, mutation, lineage |
| 8 | communicate | 62 | Light-speed messages, beacons, relay Dijkstra |
| 9 | events | 60 | Event engine, hazards, alien life generation |
| 10 | society | 92 | Trade, territory, construction, voting, tech share |
| 11 | agent_llm | 57 | Prompt building, JSON parsing, cost tracking |
| 12 | scenario | 55 | Injection, metrics, snapshots, config, replay |

Run individual phases: `make test3` (travel tests), `make test9` (events), etc.

## Key Design Decisions

**Deterministic from seed.** Same galaxy seed produces the same universe on any platform. The xoshiro256** PRNG is platform-independent and the generation pipeline is purely functional.

**Lazy generation.** The galaxy has ~100 billion potential star systems, but only materializes systems as probes explore them. Seed-derived RNG makes this repeatable.

**Personality is emergent.** Probes start with traits inherited (and mutated) from their parent. Events drift personality over time. A probe that survives many hazards becomes cautious. One that finds beautiful systems becomes more curious. Earth memories fade across generations.

**No external dependencies for core sim.** The C simulation runs headless with just SQLite. Raylib visualization and LLM agents are optional layers.

## Documentation

Detailed documentation lives in the `docs/` folder:

- [Getting Started](docs/getting-started.md) — Build, run, and configure
- [Architecture](docs/architecture.md) — Module map, data flow, struct relationships
- [Phase Guide](docs/phases.md) — All 12 phases explained
- [Scenario Guide](docs/scenario-guide.md) — Event injection, metrics, snapshots, forking
- [LLM Agent](docs/llm-agent.md) — Python agent setup, API configuration, deliberation
- [API Reference](docs/api-reference.md) — All public APIs per module
- [Testing](docs/testing.md) — Test philosophy, running tests, adding new tests

## License

See SPEC.md for the full vision specification.
