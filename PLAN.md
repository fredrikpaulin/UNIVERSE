# Plan: Bun Central Server for Project UNIVERSE

## Step 0: Restructure Project

Reorganize the flat layout into three clear domains: simulation, server, and agents.

### Current layout:
```
UNIVERSE/
  src/          ← C headers + source mixed together
  tools/        ← C test files
  agents/       ← just agent_llm.py
  vendor/       ← sqlite3, raylib
  docs/         ← documentation
  Makefile      ← builds everything
  *.o, test_*   ← binaries scattered in root
```

### New layout:
```
UNIVERSE/
  sim/                          ← The C simulation (self-contained)
    src/                        ← All .h and .c files (moved from /src)
    tests/                      ← All test_*.c files (moved from /tools)
    vendor/                     ← sqlite3.h, raylib (moved from /vendor)
    Makefile                    ← Updated paths, builds into sim/build/
    build/                      ← Compiled binaries go here (gitignored)

  server/                       ← The Bun central server (new)
    src/
      index.js                  ← Entry point: Bun.serve() + spawn sim
      process.js                ← Spawn C child, pipe JSON, lifecycle
      tick.js                   ← Tick loop, agent timeout, fallback
      agents.js                 ← WebSocket agent registry + routing
      api.js                    ← REST route handlers
      dashboard.js              ← Dashboard WebSocket streaming
      protocol.js               ← Shared JSON line protocol helpers
    test/
      process.test.js           ← Process spawn/pipe tests
      tick.test.js              ← Tick sync tests
      agents.test.js            ← Agent registration tests
      api.test.js               ← REST endpoint tests
      e2e.test.js               ← Full integration tests
    package.json

  agents/                       ← Agent implementations
    llm/
      agent.py                  ← Python LLM agent (moved from agents/)
      requirements.txt
    example/
      random-agent.js           ← Simple Bun example agent
      greedy-miner.js           ← Example: always mine

  docs/                         ← Documentation (stays)
  README.md
  SPEC.md
  .gitignore
```

### What moves:
- `src/*` → `sim/src/`
- `tools/*` → `sim/tests/`
- `vendor/*` → `sim/vendor/`
- `Makefile` → `sim/Makefile` (update all paths: `src/` stays as-is since it's relative)
- `agents/agent_llm.py` → `agents/llm/agent.py`
- Root binaries (`test_*`, `universe`, `*.o`) → `sim/build/` via Makefile change
- Symlinks (`libsqlite3.so`, `libGL.so`) → `sim/` directory
- `clean_run.sh` → `sim/clean_run.sh` (update paths)
- `data/` → `sim/data/`

### Makefile changes:
- Output binaries to `build/` subdirectory
- `make test` runs from `sim/` directory
- Add `make pipe` target that builds with `--pipe` support

### .gitignore updates:
- Add `sim/build/`, `server/node_modules/`, `*.db`

### Verification:
- `cd sim && make test` → all 1,515 tests still pass
- All doc links still work (docs/ stays at root)

---

## Phase 1: C Pipe Mode (`--pipe`)

Add a `--pipe` flag to the C simulation that turns it into a stdin/stdout JSON server. Instead of running its own tick loop, it waits for commands on stdin and responds on stdout.

### Command protocol (stdin → C):

```json
{"cmd":"tick","actions":{"0-1":{"type":"wait"},"0-2":{"type":"mine","resource":"iron"}}}
{"cmd":"status"}
{"cmd":"metrics"}
{"cmd":"inject","event":{"type":"hazard","subtype":0,"severity":0.8}}
{"cmd":"snapshot","tag":"checkpoint_1"}
{"cmd":"restore","tag":"checkpoint_1"}
{"cmd":"config","data":{"event_freq_discovery":"0.01"}}
{"cmd":"quit"}
```

The `tick` command is the core loop: it receives actions for all probes (keyed by "hi-lo" UID string), executes one tick (travel, energy, events, action execution), and returns observations.

### Response protocol (C → stdout):

```json
{"ok":true,"tick":1001,"observations":[{"probe_id":"0-1","name":"Bob","status":"active","hull":0.95,"energy":490000,"fuel":980000,"location":"in_system","system":"Alpha-7","tech":[3,3,3,3,3,3,3,3,3,3],"events":[]},{"probe_id":"0-2",...}]}
{"ok":true,"metrics":{"tick":1000,"probes_spawned":2,"avg_tech":4.0,"avg_trust":0.5}}
{"ok":true,"snapshot":"checkpoint_1"}
{"ok":false,"error":"unknown command"}
```

### Files modified:
- `sim/src/main.c` — Add `--pipe` flag, add `run_pipe_mode()` function with command read loop
- `sim/src/agent_ipc.h` — Add `pipe_write_observations()`, `pipe_read_line()`
- `sim/src/agent_ipc.c` — Implement pipe I/O functions

### What `run_pipe_mode()` does:
1. Initialize universe (seed, Bob)
2. Generate origin sector
3. Loop: read JSON line from stdin → parse cmd → execute → write JSON line to stdout → flush
4. For `tick` cmd: apply actions to probes → travel_tick → energy_tick → events_tick → serialize all probe observations → write
5. For `status`: write probe summary (id, name, status, position for each)
6. For `metrics`: call metrics_record if needed, return metrics_latest
7. For `inject/snapshot/restore/config`: delegate to scenario.c functions

### Key detail:
- All stdout writes must be flushed immediately (`fflush(stdout)`) so the Bun parent process gets data without buffering delays
- stderr is free for debug logging
- One JSON line per response, newline terminated

### Testing:
```bash
cd sim && make
echo '{"cmd":"status"}' | ./build/universe --pipe --seed 42
echo '{"cmd":"tick","actions":{}}' | ./build/universe --pipe --seed 42
# Multi-command test via heredoc
```

---

## Phase 2: Bun Process Manager + Protocol

Spawn the C sim, manage pipes, implement the JSON line protocol.

### Files:
- `server/package.json` — Bun project, no dependencies
- `server/src/protocol.js` — Read/write newline-delimited JSON on streams
- `server/src/process.js` — Spawn C binary, send commands, receive responses
- `server/test/process.test.js` — Spawn sim, send status, verify response

### `protocol.js` (~40 lines):
- `createLineReader(stream)` — async iterator that yields parsed JSON objects from a readable stream, splitting on newlines
- `writeLine(stream, obj)` — JSON.stringify + newline + write

### `process.js` (~80 lines):
- `spawnSim({ seed, simPath })` — calls `Bun.spawn()` with `--pipe --seed N`, returns handle
- `sendCommand(handle, cmd)` — write command, wait for response line, parse JSON, return
- `stopSim(handle)` — send quit command, wait for process exit with timeout, kill if needed

### `process.test.js`:
```js
import { test, expect } from "bun:test";
import { spawnSim, sendCommand, stopSim } from "../src/process.js";

test("spawn and get status", async () => {
  const sim = await spawnSim({ seed: 42 });
  const res = await sendCommand(sim, { cmd: "status" });
  expect(res.ok).toBe(true);
  expect(res.probes.length).toBeGreaterThan(0);
  await stopSim(sim);
});

test("tick advances", async () => {
  const sim = await spawnSim({ seed: 42 });
  const r1 = await sendCommand(sim, { cmd: "tick", actions: {} });
  const r2 = await sendCommand(sim, { cmd: "tick", actions: {} });
  expect(r2.tick).toBe(r1.tick + 1);
  await stopSim(sim);
});
```

### Depends on: Phase 1 (C pipe mode)

---

## Phase 3: Tick Coordinator + Agent Registry

Implement the tick loop with agent synchronization and timeout-based fallback.

### Files:
- `server/src/agents.js` — Agent registry (probe_id → WebSocket map)
- `server/src/tick.js` — Tick loop: route observations, collect actions, apply timeouts

### `agents.js` (~60 lines):
- `agents` Map — probe_id string → { ws, pendingResolve }
- `register(probeId, ws)` — associate WebSocket with probe
- `unregister(probeId)` — remove
- `getAgent(probeId)` — lookup
- `listAgents()` — all connected probe IDs
- `sendObservation(probeId, obs)` — send JSON to agent's WebSocket
- `waitForAction(probeId, timeoutMs)` — returns Promise that resolves with agent's action or fallback on timeout

### `tick.js` (~100 lines):
- `createTickLoop({ sim, tickRate, agentTimeout })` — returns start/stop controls
- Each tick:
  1. For each connected agent, send its observation from last tick response
  2. `Promise.allSettled()` on all agent action promises with timeout
  3. Build actions object: `{ "0-1": action_from_agent_or_fallback, "0-2": ... }`
  4. `sendCommand(sim, { cmd: "tick", actions })`
  5. Store observations for next iteration
  6. Emit 'tick' event for dashboard subscribers
- Fallback action: `{ type: "wait" }` for agents that don't respond in time
- Tick timing: `setTimeout` or `setInterval` with drift correction

### `tick.test.js`:
- Test: tick loop runs N ticks at configured rate
- Test: unresponsive agent gets fallback action
- Test: agent response is applied correctly

### Depends on: Phase 2

---

## Phase 4: WebSocket Server + REST API

The main server entry point. Agents connect via WebSocket, external tools use REST.

### Files:
- `server/src/index.js` — Bun.serve() with fetch handler + WebSocket handlers
- `server/src/api.js` — REST route handlers (pure functions, sim handle passed in)

### `index.js` (~120 lines):
```js
const sim = await spawnSim({ seed: config.seed });
const tickLoop = createTickLoop({ sim, tickRate: 10, agentTimeout: 5000 });

Bun.serve({
  port: 8000,
  fetch(req, server) {
    const url = new URL(req.url);

    // WebSocket upgrade
    if (url.pathname === "/ws") return server.upgrade(req);
    if (url.pathname === "/ws/dashboard") return server.upgrade(req, { data: { dashboard: true } });

    // REST API
    return handleAPI(url, req, sim);
  },
  websocket: {
    open(ws) { /* noop until registered */ },
    message(ws, msg) {
      const data = JSON.parse(msg);
      if (data.type === "register") register(data.probe_id, ws);
      else if (data.actions) resolveAction(data.probe_id, data);
    },
    close(ws) { unregisterByWs(ws); }
  }
});

tickLoop.start();
```

### `api.js` (~100 lines):
```
GET  /api/status              → sendCommand(sim, {cmd:"status"})
GET  /api/metrics             → sendCommand(sim, {cmd:"metrics"})
GET  /api/probes              → status response filtered to probe list
GET  /api/probes/:id          → single probe from status
POST /api/tick                → manual single tick (when paused)
POST /api/inject              → sendCommand(sim, {cmd:"inject", event: body})
POST /api/snapshot            → sendCommand(sim, {cmd:"snapshot", tag: body.tag})
POST /api/restore             → sendCommand(sim, {cmd:"restore", tag: body.tag})
POST /api/config              → sendCommand(sim, {cmd:"config", data: body})
GET  /api/agents              → list connected agents
POST /api/pause               → tickLoop.pause()
POST /api/resume              → tickLoop.resume()
```

All handlers are `async (url, req, sim) => Response.json(...)` — no middleware, no framework.

### `api.test.js`:
- Test each REST endpoint against a running server
- Use `fetch()` directly (Bun built-in)

### Depends on: Phase 3

---

## Phase 5: Dashboard Streaming + Example Agents

Live observation streaming for monitoring, plus example agents that demonstrate the protocol.

### Files:
- `server/src/dashboard.js` — Dashboard subscriber management, tick event formatting
- `agents/example/random-agent.js` — Connects via WebSocket, picks random valid actions
- `agents/example/greedy-miner.js` — Always tries to mine, falls back to survey/navigate

### `dashboard.js` (~50 lines):
- Maintains Set of dashboard WebSockets
- On each tick completion, broadcasts:
  ```json
  {"type":"tick","tick":1001,"probes":[...],"metrics":{...},"agents":{"connected":2,"timed_out":0}}
  ```
- Dashboard clients connect to `/ws/dashboard`, receive stream, no registration needed

### `random-agent.js` (~60 lines):
```js
const ws = new WebSocket("ws://localhost:8000/ws");
ws.onopen = () => ws.send(JSON.stringify({ type: "register", probe_id: "0-1" }));
ws.onmessage = (e) => {
  const msg = JSON.parse(e.data);
  if (msg.cmd === "observe") {
    const actions = ["wait", "survey", "mine", "repair"];
    const pick = actions[Math.floor(Math.random() * actions.length)];
    ws.send(JSON.stringify({ actions: [{ type: pick }] }));
  }
};
```

### `greedy-miner.js` (~80 lines):
- If landed and resources available → mine iron
- If orbiting → land
- If in system → navigate to richest planet
- If damaged → repair
- Otherwise → wait

### `agents/llm/agent.py` (updated):
- Change socket connection from Unix socket to WebSocket (`ws://localhost:8000/ws`)
- Same registration protocol, same LLM call logic

### End-to-end test:
```bash
# Terminal 1: Start server
cd server && bun run src/index.js --seed 42

# Terminal 2: Connect example agent
bun run ../agents/example/random-agent.js

# Terminal 3: Watch dashboard
bun run ../agents/example/dashboard-client.js

# Terminal 4: Poke REST API
curl localhost:8000/api/status | jq .
curl localhost:8000/api/metrics | jq .
curl -X POST localhost:8000/api/inject -d '{"event":{"type":"hazard","subtype":0,"severity":0.8}}'
```

### Depends on: Phase 4

---

## Phase 6: Integration Tests + Docs

### Files:
- `server/test/e2e.test.js` — Full end-to-end scenarios
- `docs/server.md` — Server documentation
- `docs/agent-protocol.md` — WebSocket agent protocol reference
- Updated `README.md` — New project structure, server instructions

### `e2e.test.js` scenarios:
1. Spawn server → connect 2 agents → run 20 ticks → verify both received observations
2. Agent timeout → verify fallback action applied → verify probe didn't crash
3. Inject event mid-run → verify probe hull decreased
4. Snapshot at tick 10 → run to tick 20 → restore → verify tick is 10 again
5. Config change → verify event frequency changed
6. Dashboard client → verify receives tick stream
7. REST /api/probes → verify matches simulation state
8. Agent disconnect → verify probe gets fallback → agent reconnect → resumes

### `docs/server.md`:
- Architecture diagram (Bun ↔ C child process ↔ agents)
- Configuration options (port, tick rate, timeout, seed)
- Starting the server
- REST API reference with curl examples
- Performance notes

### `docs/agent-protocol.md`:
- WebSocket connection flow
- Registration message format
- Observation format (full JSON schema)
- Action response format (full JSON schema)
- Timeout behavior
- Error handling
- Example agent walkthrough

### Depends on: Phase 5

---

## Summary

| Phase | What | Key Output | LOC Estimate |
|-------|------|-----------|-------------|
| 0 | Restructure folders | Clean sim/server/agents split | ~0 new code, moved files |
| 1 | C --pipe mode | `./universe --pipe` reads stdin, writes stdout | ~300 lines C |
| 2 | Bun process manager | Spawn sim, send/receive JSON | ~150 lines JS |
| 3 | Tick coordinator | Tick loop with agent sync + timeout | ~200 lines JS |
| 4 | WebSocket + REST | Full server with API | ~250 lines JS |
| 5 | Dashboard + example agents | Live streaming, working agents | ~200 lines JS |
| 6 | Integration tests + docs | E2E tests, full documentation | ~400 lines JS + docs |
