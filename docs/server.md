# Universe Server

The universe server wraps the C simulation as a child process and exposes it over WebSocket (for agents) and REST (for tools and dashboards). It handles tick synchronization, agent timeouts, and live streaming.

## Architecture

```
                    ┌──────────────┐
                    │  Bun Server  │
                    │  (index.js)  │
                    └──┬───┬───┬──┘
          ┌────────────┘   │   └────────────┐
          ▼                ▼                ▼
    ┌──────────┐   ┌─────────────┐   ┌───────────┐
    │ WebSocket │   │  REST API   │   │ Dashboard  │
    │  /ws      │   │  /api/*     │   │  /ws/dash  │
    │ (agents)  │   │  (api.js)   │   │  board     │
    └─────┬────┘   └──────┬──────┘   └─────┬─────┘
          │               │                 │
          ▼               ▼                 ▼
    ┌──────────┐   ┌─────────────┐   ┌───────────┐
    │  Agent    │   │    Tick     │   │ Broadcast  │
    │ Registry  │   │ Coordinator │   │  (events)  │
    │(agents.js)│   │  (tick.js)  │   │(dashboard) │
    └─────┬────┘   └──────┬──────┘   └───────────┘
          │               │
          └───────┬───────┘
                  ▼
          ┌──────────────┐
          │  C Sim Child  │
          │  (--pipe)     │
          │  stdin/stdout │
          │  JSON lines   │
          └──────────────┘
```

The C simulation runs as a child process communicating via newline-delimited JSON on stdin/stdout. The server sends tick commands with aggregated agent actions and receives observations in response.

## Quick Start

```bash
# From the project root
cd sim && make           # Build the C simulation
cd ../server && npm install  # Install bun dependency
npx bun run src/index.js --seed 42
```

The server prints connection URLs on startup:

```
[universe] server listening on http://localhost:8000
[universe] WebSocket agents: ws://localhost:8000/ws
[universe] Dashboard stream: ws://localhost:8000/ws/dashboard
[universe] tick loop started (rate=10/s, timeout=5000ms)
```

## Configuration

All options are passed as CLI flags:

| Flag | Default | Description |
|------|---------|-------------|
| `--seed N` | 42 | Galaxy seed for the simulation |
| `--port N` | 8000 | HTTP/WebSocket server port |
| `--tick-rate N` | 10 | Ticks per second |
| `--agent-timeout N` | 5000 | Milliseconds to wait for agent actions before fallback |

Example with all options:

```bash
npx bun run src/index.js --seed 12345 --port 3000 --tick-rate 5 --agent-timeout 10000
```

## REST API

All endpoints return JSON. POST endpoints accept JSON bodies.

### Simulation State

**GET /api/status** — Full simulation status with probe list.

```bash
curl localhost:8000/api/status
```

```json
{"ok":true,"tick":42,"probes":[{"id":"1-1","name":"Bob","status":"active","location":"in_system","generation":0}]}
```

**GET /api/metrics** — Current simulation metrics.

```bash
curl localhost:8000/api/metrics
```

```json
{"ok":true,"tick":42,"probes_spawned":1,"avg_tech":2.30,"avg_trust":0.000,"systems_explored":1,"total_discoveries":0,"total_hazards_survived":0}
```

**GET /api/probes** — Probe list only.

```bash
curl localhost:8000/api/probes
```

**GET /api/probes/:id** — Single probe by "hi-lo" ID string.

```bash
curl localhost:8000/api/probes/1-1
```

### Tick Control

**POST /api/tick** — Execute a single manual tick. Useful when the loop is paused or stopped.

```bash
curl -X POST localhost:8000/api/tick
```

**POST /api/pause** — Pause the automatic tick loop.

```bash
curl -X POST localhost:8000/api/pause
```

**POST /api/resume** — Resume the tick loop.

```bash
curl -X POST localhost:8000/api/resume
```

**GET /api/state** — Current tick loop state.

```bash
curl localhost:8000/api/state
```

```json
{"ok":true,"state":{"running":true,"paused":false,"tick":42,"agents":1}}
```

### Scenario Control

**POST /api/inject** — Queue an event for the next tick.

```bash
curl -X POST localhost:8000/api/inject \
  -H "Content-Type: application/json" \
  -d '{"type":"hazard","subtype":0,"severity":0.8}'
```

**POST /api/snapshot** — Save simulation state.

```bash
curl -X POST localhost:8000/api/snapshot \
  -H "Content-Type: application/json" \
  -d '{"tag":"checkpoint_1"}'
```

**POST /api/restore** — Rollback to a saved snapshot.

```bash
curl -X POST localhost:8000/api/restore \
  -H "Content-Type: application/json" \
  -d '{"tag":"checkpoint_1"}'
```

**POST /api/config** — Set simulation configuration values.

```bash
curl -X POST localhost:8000/api/config \
  -H "Content-Type: application/json" \
  -d '{"event_freq_discovery":"0.01","mutation_rate":"0.15"}'
```

### Agents

**GET /api/agents** — List connected agent probe IDs.

```bash
curl localhost:8000/api/agents
```

```json
{"ok":true,"agents":["1-1"]}
```

## Tick Loop

The tick coordinator runs at a configurable rate (default 10 ticks/sec). Each tick follows this sequence:

1. Send observations from the previous tick to all connected agents
2. Wait for agent action responses (up to `--agent-timeout` ms)
3. Build an actions object — agents that don't respond in time get `{"action":"wait"}`
4. Send the tick command with all actions to the C simulation
5. Receive observations for all probes
6. Broadcast tick event to dashboard subscribers

The loop can be paused and resumed via the REST API. Manual ticks via `POST /api/tick` work even when the loop is paused.

## File Structure

```
server/
  src/
    index.js      Entry point: CLI args, spawn sim, Bun.serve()
    process.js    Spawn C child, JSON pipe protocol
    protocol.js   Newline-delimited JSON stream reader/writer
    tick.js       Tick loop with agent sync and timeout
    agents.js     WebSocket agent registry
    api.js        REST route handlers
    dashboard.js  Dashboard subscriber broadcast
  test/
    process.test.js   Process spawn/pipe tests
    tick.test.js      Tick sync + agent timeout tests
    api.test.js       REST endpoint tests
    e2e.test.js       Full integration tests
  package.json
```

## Running Tests

```bash
cd server
npx bun test
```

Tests spawn real C simulation processes and real HTTP/WebSocket servers on random ports. No mocking of the simulation — every test exercises the full stack.
