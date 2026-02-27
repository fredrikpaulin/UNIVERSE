/**
 * random-agent.js — Example agent that picks random actions.
 *
 * Usage: bun run agents/example/random-agent.js [--url ws://localhost:8000/ws] [--probe 1-1]
 *
 * Connects to the universe server, registers for a probe, and responds
 * to each observation with a random valid action.
 */

const args = process.argv.slice(2);
let url = "ws://localhost:8000/ws";
let probeId = null;

for (let i = 0; i < args.length; i++) {
  if (args[i] === "--url" && args[i + 1]) url = args[++i];
  if (args[i] === "--probe" && args[i + 1]) probeId = args[++i];
}

const ACTIONS = ["wait", "survey", "mine", "repair"];

function pickAction(obs) {
  const action = ACTIONS[Math.floor(Math.random() * ACTIONS.length)];
  if (action === "mine") return { action: "mine", resource: "iron" };
  return { action };
}

console.log(`[random-agent] connecting to ${url}`);
const ws = new WebSocket(url);

ws.onopen = () => {
  console.log("[random-agent] connected");

  if (probeId) {
    ws.send(JSON.stringify({ type: "register", probe_id: probeId }));
    console.log(`[random-agent] registered for probe ${probeId}`);
  } else {
    // Auto-discover: fetch status, register for first probe
    fetch(url.replace("ws://", "http://").replace("/ws", "/api/status"))
      .then((r) => r.json())
      .then((status) => {
        if (status.probes?.length) {
          probeId = status.probes[0].id;
          ws.send(JSON.stringify({ type: "register", probe_id: probeId }));
          console.log(`[random-agent] auto-registered for probe ${probeId}`);
        }
      })
      .catch((e) => console.error("[random-agent] auto-discover failed:", e));
  }
};

ws.onmessage = (e) => {
  const msg = JSON.parse(e.data);

  if (msg.type === "registered") {
    console.log(`[random-agent] confirmed registration for ${msg.probe_id}`);
    return;
  }

  if (msg.type === "observe") {
    const action = pickAction(msg);
    console.log(
      `[random-agent] tick=${msg.tick || "?"} status=${msg.status} hull=${msg.hull} → ${action.action}`
    );
    ws.send(JSON.stringify(action));
  }
};

ws.onclose = () => {
  console.log("[random-agent] disconnected");
  process.exit(0);
};

ws.onerror = (e) => {
  console.error("[random-agent] error:", e.message);
};
