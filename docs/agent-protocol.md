# Agent WebSocket Protocol

Agents connect to the universe server via WebSocket and control one probe each. The server sends observations after every tick, and agents respond with actions.

## Connection Flow

```
Agent                          Server
  │                              │
  │──── WebSocket connect ──────▶│  ws://localhost:8000/ws
  │                              │
  │──── register ──────────────▶│  {"type":"register","probe_id":"1-1"}
  │                              │
  │◀─── registered ─────────────│  {"type":"registered","probe_id":"1-1"}
  │                              │
  │         ┌── tick loop ──┐    │
  │         │               │    │
  │◀────────│─ observe ─────│────│  {"type":"observe","probe_id":"1-1",...}
  │         │               │    │
  │─────────│─ action ──────│───▶│  {"action":"survey"}
  │         │               │    │
  │         └── repeats ────┘    │
  │                              │
  │──── close ─────────────────▶│  (probe falls back to "wait")
```

## Registration

After opening a WebSocket connection to `/ws`, the agent must register for a specific probe by sending:

```json
{"type": "register", "probe_id": "1-1"}
```

The `probe_id` is a string in `"hi-lo"` format (two integers separated by a dash). You can discover available probe IDs via the REST API: `GET /api/status`.

The server confirms registration:

```json
{"type": "registered", "probe_id": "1-1"}
```

Only one agent can control a probe at a time. Registering for an already-controlled probe replaces the previous agent.

## Observation Format

After each tick, the server sends an observation to the agent:

```json
{
  "type": "observe",
  "probe_id": "1-1",
  "name": "Bob",
  "status": "active",
  "hull": 0.95,
  "energy": 1629998000000.0,
  "fuel": 50000.0,
  "location": "in_system",
  "generation": 0,
  "tech": [3, 3, 2, 2, 4, 3, 2, 2, 1, 1]
}
```

### Observation Fields

| Field | Type | Description |
|-------|------|-------------|
| `type` | string | Always `"observe"` |
| `probe_id` | string | Probe UID in `"hi-lo"` format |
| `name` | string | Probe name (e.g. "Bob") |
| `status` | string | One of: `active`, `traveling`, `mining`, `building`, `replicating`, `dormant`, `damaged`, `destroyed` |
| `hull` | number | Hull integrity, 0.0–1.0 |
| `energy` | number | Energy in joules |
| `fuel` | number | Fuel in kg |
| `location` | string | One of: `interstellar`, `in_system`, `orbiting`, `landed`, `docked` |
| `generation` | number | Probe generation (0 = original Bob) |
| `tech` | number[] | Array of 10 tech levels: propulsion, sensors, mining, construction, computing, energy, materials, communication, weapons, biotech |

## Action Format

The agent must respond with a single action before the timeout expires:

```json
{"action": "survey"}
```

### Available Actions

| Action | Fields | Description |
|--------|--------|-------------|
| `wait` | — | Do nothing this tick |
| `survey` | — | Survey current body (progressive levels 0–4) |
| `mine` | `resource` | Mine a resource. Resource names: `iron`, `silicon`, `rare_earth`, `water`, `hydrogen`, `helium3`, `carbon`, `uranium`, `exotic` |
| `repair` | — | Self-repair hull |
| `navigate_to_body` | `target_body_hi`, `target_body_lo` | Move to a planet/moon in current system |
| `enter_orbit` | — | Enter orbit around current body |
| `land` | — | Land on body surface |
| `launch` | — | Launch from surface to orbit |

### Action Examples

```json
{"action": "wait"}
{"action": "survey"}
{"action": "mine", "resource": "iron"}
{"action": "repair"}
{"action": "land"}
{"action": "launch"}
{"action": "navigate_to_body", "target_body_hi": 123, "target_body_lo": 456}
```

Actions are validated against the probe's current state. Invalid actions (e.g. mining while in orbit) are rejected and the probe effectively waits.

## Timeout Behavior

The server waits up to `--agent-timeout` milliseconds (default 5000) for each agent to respond with an action. If the timeout expires, the probe receives a fallback action of `{"action": "wait"}`.

This means agents don't need to be fast — they have several seconds to deliberate. LLM-based agents can make API calls within this window.

## Error Handling

If the WebSocket connection drops, the probe automatically receives fallback actions until a new agent registers for it. There is no penalty for disconnecting.

If the agent sends malformed JSON, the message is silently ignored. The agent will time out and the probe will get a fallback action.

## Dashboard Protocol

Dashboard clients connect to `/ws/dashboard` and receive tick broadcasts. No registration is needed — just connect and listen.

Each tick broadcasts:

```json
{
  "type": "tick",
  "tick": 42,
  "observations": [
    {"probe_id": "1-1", "name": "Bob", "status": "active", "hull": 1.0, ...}
  ],
  "agents": {"connected": 1}
}
```

Dashboard connections are passive — they cannot send commands or control probes.

## Example: Minimal Agent in JavaScript

```js
const ws = new WebSocket("ws://localhost:8000/ws");

ws.onopen = () => {
  ws.send(JSON.stringify({ type: "register", probe_id: "1-1" }));
};

ws.onmessage = (e) => {
  const msg = JSON.parse(e.data);
  if (msg.type === "observe") {
    // Simple strategy: repair if damaged, otherwise survey
    const action = msg.hull < 0.5
      ? { action: "repair" }
      : { action: "survey" };
    ws.send(JSON.stringify(action));
  }
};
```

## Example: Agent in Python

```python
import asyncio, json, websockets

async def agent():
    async with websockets.connect("ws://localhost:8000/ws") as ws:
        await ws.send(json.dumps({"type": "register", "probe_id": "1-1"}))
        async for msg in ws:
            data = json.loads(msg)
            if data["type"] == "observe":
                action = {"action": "repair"} if data["hull"] < 0.5 else {"action": "survey"}
                await ws.send(json.dumps(action))

asyncio.run(agent())
```

## Discovering Probe IDs

Agents need a probe ID to register. You can discover available probes via REST before connecting the WebSocket:

```bash
curl localhost:8000/api/status | jq '.probes[].id'
```

The example agents (`random-agent.js`, `greedy-miner.js`) demonstrate auto-discovery by fetching `/api/status` on connect and registering for the first available probe.
