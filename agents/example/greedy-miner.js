/**
 * greedy-miner.js — Example agent that prioritizes mining.
 *
 * Usage: bun run agents/example/greedy-miner.js [--url ws://localhost:8000/ws] [--probe 1-1]
 *
 * Strategy:
 *   - If damaged (hull < 0.5) → repair
 *   - If landed → mine iron
 *   - If orbiting → land
 *   - If in system → survey (to discover resources)
 *   - Otherwise → wait
 */

const args = process.argv.slice(2);
let url = "ws://localhost:8000/ws";
let probeId = null;

for (let i = 0; i < args.length; i++) {
  if (args[i] === "--url" && args[i + 1]) url = args[++i];
  if (args[i] === "--probe" && args[i + 1]) probeId = args[++i];
}

function decide(obs) {
  // Emergency repair if badly damaged
  if (obs.hull < 0.5) return { action: "repair" };

  switch (obs.location) {
    case "landed":
      return { action: "mine", resource: "iron" };
    case "orbiting":
      return { action: "land" };
    case "in_system":
      return { action: "survey" };
    default:
      return { action: "wait" };
  }
}

console.log(`[greedy-miner] connecting to ${url}`);
const ws = new WebSocket(url);

ws.onopen = () => {
  console.log("[greedy-miner] connected");

  if (probeId) {
    ws.send(JSON.stringify({ type: "register", probe_id: probeId }));
  } else {
    fetch(url.replace("ws://", "http://").replace("/ws", "/api/status"))
      .then((r) => r.json())
      .then((status) => {
        if (status.probes?.length) {
          probeId = status.probes[0].id;
          ws.send(JSON.stringify({ type: "register", probe_id: probeId }));
          console.log(`[greedy-miner] registered for probe ${probeId}`);
        }
      })
      .catch((e) => console.error("[greedy-miner] discover failed:", e));
  }
};

ws.onmessage = (e) => {
  const msg = JSON.parse(e.data);

  if (msg.type === "registered") {
    console.log(`[greedy-miner] controlling probe ${msg.probe_id}`);
    return;
  }

  if (msg.type === "observe") {
    const action = decide(msg);
    console.log(
      `[greedy-miner] tick=${msg.tick || "?"} loc=${msg.location} hull=${msg.hull} → ${action.action}`
    );
    ws.send(JSON.stringify(action));
  }
};

ws.onclose = () => {
  console.log("[greedy-miner] disconnected");
  process.exit(0);
};

ws.onerror = (e) => console.error("[greedy-miner] error:", e.message);
