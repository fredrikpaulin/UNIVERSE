/**
 * index.js — Universe server entry point.
 *
 * Spawns the C simulation, starts tick loop, serves WebSocket + REST.
 *
 * Usage: bun run src/index.js [--seed N] [--port N] [--tick-rate N] [--agent-timeout N]
 */

import { spawnSim, stopSim } from "./process.js";
import { createTickLoop } from "./tick.js";
import { handleAPI } from "./api.js";
import { register, unregisterByWs, resolveAction } from "./agents.js";
import { addClient, removeClient, broadcast } from "./dashboard.js";

/* ---- CLI args ---- */

function parseArgs(args) {
  const cfg = { seed: 42, port: 8000, tickRate: 10, agentTimeout: 5000 };
  for (let i = 0; i < args.length; i++) {
    if (args[i] === "--seed" && args[i + 1]) cfg.seed = +args[++i];
    if (args[i] === "--port" && args[i + 1]) cfg.port = +args[++i];
    if (args[i] === "--tick-rate" && args[i + 1]) cfg.tickRate = +args[++i];
    if (args[i] === "--agent-timeout" && args[i + 1]) cfg.agentTimeout = +args[++i];
  }
  return cfg;
}

const cfg = parseArgs(process.argv.slice(2));

/* ---- Startup ---- */

console.log(`[universe] starting sim seed=${cfg.seed}`);
const sim = await spawnSim({ seed: cfg.seed });
console.log(`[universe] sim ready, tick=0`);

const tickLoop = createTickLoop({
  sim,
  tickRate: cfg.tickRate,
  agentTimeout: cfg.agentTimeout,
});

tickLoop.on("tick", (e) => broadcast(e));

tickLoop.on("error", (e) => {
  console.error("[universe] tick error:", e);
});

/* ---- Server ---- */

const server = Bun.serve({
  port: cfg.port,

  async fetch(req, server) {
    const url = new URL(req.url);

    if (url.pathname === "/ws") {
      if (server.upgrade(req, { data: { type: "agent" } })) return;
      return new Response("WebSocket upgrade failed", { status: 400 });
    }

    if (url.pathname === "/ws/dashboard") {
      if (server.upgrade(req, { data: { type: "dashboard" } })) return;
      return new Response("WebSocket upgrade failed", { status: 400 });
    }

    return handleAPI(url, req, { sim, tickLoop });
  },

  websocket: {
    open(ws) {
      if (ws.data?.type === "dashboard") addClient(ws);
    },

    message(ws, msg) {
      try {
        const data = JSON.parse(msg);
        if (data.type === "register" && data.probe_id) {
          register(data.probe_id, ws);
          ws.send(JSON.stringify({ type: "registered", probe_id: data.probe_id }));
        } else if (data.action || data.actions) {
          const probeId = ws._probeId || data.probe_id;
          if (probeId) resolveAction(probeId, data);
        }
      } catch (_) {}
    },

    close(ws) {
      if (ws.data?.type === "dashboard") removeClient(ws);
      else unregisterByWs(ws);
    },
  },
});

console.log(`[universe] server listening on http://localhost:${server.port}`);
console.log(`[universe] WebSocket agents: ws://localhost:${server.port}/ws`);
console.log(`[universe] Dashboard stream: ws://localhost:${server.port}/ws/dashboard`);

tickLoop.start();
console.log(`[universe] tick loop started (rate=${cfg.tickRate}/s, timeout=${cfg.agentTimeout}ms)`);

/* ---- Graceful shutdown ---- */

process.on("SIGINT", async () => {
  console.log("\n[universe] shutting down...");
  tickLoop.stop();
  await stopSim(sim);
  server.stop();
  process.exit(0);
});

export { sim, tickLoop, server, cfg };
