# Project UNIVERSE

A persistent, procedurally generated universe simulation with sentient AI Von Neumann probes. Inspired by Dennis E. Taylor's Bobiverse series.

Earth is dying. As a last act, humanity launches a Von Neumann probe carrying a digitized human consciousness into the void. The probe wakes up alone in an unfamiliar star system with one directive: *survive, replicate, explore*.

The simulation is three things at once: a reasonably accurate procedural universe with real astrophysics driving generation, a sandbox for sentient AI agents that develop personality and form societies, and a testbed for agentic AI systems making real decisions with real consequences.

Think of it as a fishtank — something you leave running and check in on. "What did the probes get up to while I was gone?"

## Quick Start

**Prerequisites:** GCC (C11), SQLite3 development libraries, Make, Bun (or Node.js 22+). Optional: Raylib (for visual mode), Python 3 + Anthropic API key (for LLM agent).

```bash
# Build the C simulation
cd sim && make && cd ..

# Start the server (spawns sim, serves agents + REST)
cd server && npm install && npx bun run src/index.js --seed 42

# In another terminal — connect an example agent
npx bun run agents/example/random-agent.js

# In another terminal — watch the dashboard stream
npx bun run agents/example/dashboard-client.js

# Poke the REST API
curl localhost:8000/api/status | jq .
curl localhost:8000/api/metrics | jq .
```

### Running the Simulation Standalone

```bash
cd sim

# Run headless simulation
make
LD_LIBRARY_PATH=. ./build/universe

# Run with Raylib visualization (requires Raylib)
make visual
LD_LIBRARY_PATH=. ./build/universe_visual

# Run in pipe mode (JSON server on stdin/stdout)
echo '{"cmd":"status"}' | LD_LIBRARY_PATH=. ./build/universe --pipe --seed 42

# Run C tests
make test
```

## Architecture at a Glance

The codebase is pure C11 with minimal dependencies: SQLite for persistence, xoshiro256** for deterministic RNG, and optionally Raylib for visualization. No other libraries.

```
sim/                    C simulation engine
  src/
    universe.h          Core types: probes, stars, planets, systems
    rng.h/c             Seeded PRNG (xoshiro256**)
    arena.h/c           Bump allocator for scratch memory
    persist.h/c         SQLite persistence layer
    generate.h/c        Procedural galaxy generation
    probe.h/c           Probe actions and state management
    travel.h/c          Interstellar travel and sensors
    agent_ipc.h/c       Agent protocol (JSON over Unix sockets)
    render.h/c          View state, camera, speed control
    personality.h/c     Personality drift, memory, monologue, quirks
    replicate.h/c       Self-replication with personality mutation
    communicate.h/c     Light-speed messaging, beacons, relay satellites
    events.h/c          Event engine, hazards, alien civilizations
    society.h/c         Relationships, trade, territory, voting, tech sharing
    agent_llm.h/c       LLM prompt building, response parsing, cost tracking
    scenario.h/c        Event injection, metrics, snapshots, config, replay
  tests/
    test_*.c            Test suites for each phase (1,515 tests total)
  vendor/
    sqlite3.h/c         Bundled SQLite
    raylib/              Raylib headers and prebuilt libraries
  Makefile

server/                 Bun server (wraps C sim, serves agents)
  src/
    index.js            Entry point: spawn sim, Bun.serve()
    process.js          C child process management, JSON pipes
    protocol.js         Newline-delimited JSON stream protocol
    tick.js             Tick loop with agent sync + timeout
    agents.js           WebSocket agent registry
    api.js              REST route handlers
    dashboard.js        Dashboard broadcast
  test/
    process.test.js     Process spawn/pipe tests
    tick.test.js        Tick coordinator tests
    api.test.js         REST endpoint tests
    e2e.test.js         Full integration tests

agents/                 Agent implementations
  llm/
    agent.py            Python LLM agent (Anthropic Claude API)
  example/
    random-agent.js     Picks random actions each tick
    greedy-miner.js     Prioritizes mining, repairs when damaged
    dashboard-client.js Streams and prints tick summaries
```

## Test Suite

1,515 C tests across 12 phases + 48 server tests, all passing:

### Simulation (C)

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

Run with `cd sim && make test`. Individual phases: `make test3` (travel), `make test9` (events), etc.

### Server (Bun)

| Suite | Tests | Description |
|-------|-------|-------------|
| process | 13 | C child spawn, pipe protocol, all commands |
| tick | 11 | Tick loop, agent sync, timeout fallback |
| api | 15 | REST endpoints, WebSocket agent + dashboard |
| e2e | 9 | Multi-agent, snapshot/restore, inject, config |

Run with `cd server && npx bun test`.

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
- [Server](docs/server.md) — Server architecture, REST API, configuration
- [Agent Protocol](docs/agent-protocol.md) — WebSocket protocol, observation/action formats

## License

See SPEC.md for the full vision specification.
