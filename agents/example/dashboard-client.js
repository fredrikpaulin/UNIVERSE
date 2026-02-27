/**
 * dashboard-client.js — Example dashboard that streams tick data.
 *
 * Usage: bun run agents/example/dashboard-client.js [--url ws://localhost:8000/ws/dashboard]
 *
 * Connects to the dashboard WebSocket and prints each tick's summary.
 */

const args = process.argv.slice(2);
let url = "ws://localhost:8000/ws/dashboard";

for (let i = 0; i < args.length; i++) {
  if (args[i] === "--url" && args[i + 1]) url = args[++i];
}

console.log(`[dashboard] connecting to ${url}`);
const ws = new WebSocket(url);

ws.onopen = () => console.log("[dashboard] connected, waiting for ticks...\n");

ws.onmessage = (e) => {
  const msg = JSON.parse(e.data);
  if (msg.type !== "tick") return;

  const probes = msg.observations || [];
  const probesSummary = probes
    .map((p) => `  ${p.name} [${p.status}] hull=${p.hull} energy=${(p.energy / 1e9).toFixed(1)}GJ fuel=${p.fuel.toFixed(0)}kg`)
    .join("\n");

  console.log(
    `--- Tick ${msg.tick} | ${probes.length} probe(s) | ${msg.agents?.connected || 0} agent(s) ---`
  );
  if (probesSummary) console.log(probesSummary);
  console.log();
};

ws.onclose = () => {
  console.log("[dashboard] disconnected");
  process.exit(0);
};

ws.onerror = (e) => console.error("[dashboard] error:", e.message);
