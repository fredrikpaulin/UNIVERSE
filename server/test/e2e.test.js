import { test, expect, describe, beforeAll, afterAll } from "bun:test";
import { spawnSim, stopSim } from "../src/process.js";
import { createTickLoop } from "../src/tick.js";
import { handleAPI } from "../src/api.js";
import { register, unregisterByWs, resolveAction, clear as clearAgents } from "../src/agents.js";
import { addClient, removeClient, broadcast, clear as clearDashboard } from "../src/dashboard.js";

let sim, tickLoop, server, port;

beforeAll(async () => {
  clearAgents();
  clearDashboard();
  sim = await spawnSim({ seed: 42 });
  tickLoop = createTickLoop({ sim, tickRate: 10, agentTimeout: 500 });

  tickLoop.on("tick", (e) => broadcast(e));

  server = Bun.serve({
    port: 0,
    async fetch(req, srv) {
      const url = new URL(req.url);
      if (url.pathname === "/ws") {
        if (srv.upgrade(req, { data: { type: "agent" } })) return;
        return new Response("upgrade failed", { status: 400 });
      }
      if (url.pathname === "/ws/dashboard") {
        if (srv.upgrade(req, { data: { type: "dashboard" } })) return;
        return new Response("upgrade failed", { status: 400 });
      }
      return handleAPI(url, req, { sim, tickLoop });
    },
    websocket: {
      open(ws) { if (ws.data?.type === "dashboard") addClient(ws); },
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
  port = server.port;
});

afterAll(async () => {
  tickLoop.stop();
  server.stop();
  clearAgents();
  clearDashboard();
  if (sim) await stopSim(sim);
});

/** Helper: connect a WebSocket agent that responds with a given strategy. */
function connectAgent(probeId, strategy) {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://localhost:${port}/ws`);
    const received = [];

    ws.onopen = () => {
      ws.send(JSON.stringify({ type: "register", probe_id: probeId }));
    };

    ws.onmessage = (e) => {
      const msg = JSON.parse(e.data);
      received.push(msg);
      if (msg.type === "registered") {
        resolve({ ws, received });
      }
      if (msg.type === "observe" && strategy) {
        const action = strategy(msg);
        ws.send(JSON.stringify(action));
      }
    };

    ws.onerror = reject;
    setTimeout(() => reject(new Error("agent connect timeout")), 3000);
  });
}

/** Helper: connect a dashboard client and collect ticks. */
function connectDashboard() {
  return new Promise((resolve, reject) => {
    const ws = new WebSocket(`ws://localhost:${port}/ws/dashboard`);
    const ticks = [];
    ws.onopen = () => resolve({ ws, ticks });
    ws.onmessage = (e) => {
      const msg = JSON.parse(e.data);
      if (msg.type === "tick") ticks.push(msg);
    };
    ws.onerror = reject;
    setTimeout(() => reject(new Error("dashboard connect timeout")), 3000);
  });
}

const get = (path) => fetch(`http://localhost:${port}${path}`).then((r) => r.json());
const post = (path, body) =>
  fetch(`http://localhost:${port}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  }).then((r) => r.json());

describe("end-to-end", () => {
  test("agent connects, receives observations, actions applied", async () => {
    // Run a tick first to get probe id
    const r1 = await post("/api/tick");
    const probeId = r1.observations[0].probe_id;

    // Connect agent that always surveys
    const { ws: agentWs, received } = await connectAgent(probeId, () => ({
      action: "survey",
    }));

    // Run ticks — agent should get observations
    await post("/api/tick");
    await post("/api/tick");
    await new Promise((r) => setTimeout(r, 200));

    // Agent should have received observe messages
    const observes = received.filter((m) => m.type === "observe");
    expect(observes.length).toBeGreaterThan(0);

    agentWs.close();
    await new Promise((r) => setTimeout(r, 100));
  });

  test("agent timeout produces fallback action", async () => {
    const status = await get("/api/status");
    const probeId = status.probes[0].id;

    // Connect agent that NEVER responds
    const { ws: agentWs } = await connectAgent(probeId, null);

    // Run a tick — should complete after timeout (~500ms)
    const start = Date.now();
    const resp = await post("/api/tick");
    const elapsed = Date.now() - start;

    expect(resp.ok).toBe(true);
    expect(elapsed).toBeGreaterThanOrEqual(400);
    // Probe should be fine (fallback = wait)
    expect(resp.observations[0].status).toBe("active");

    agentWs.close();
    await new Promise((r) => setTimeout(r, 100));
  });

  test("dashboard receives tick broadcasts", async () => {
    const { ws: dashWs, ticks } = await connectDashboard();

    await post("/api/tick");
    await post("/api/tick");
    await new Promise((r) => setTimeout(r, 200));

    expect(ticks.length).toBeGreaterThanOrEqual(2);
    expect(ticks[0].type).toBe("tick");
    expect(typeof ticks[0].tick).toBe("number");
    expect(ticks[0].observations.length).toBeGreaterThan(0);

    dashWs.close();
    await new Promise((r) => setTimeout(r, 100));
  });

  test("inject event mid-run affects probe", async () => {
    // Snapshot current state
    await post("/api/snapshot", { tag: "e2e_pre_inject" });

    // Inject a severe hazard
    const inj = await post("/api/inject", {
      type: "hazard", subtype: 0, severity: 0.9,
    });
    expect(inj.ok).toBe(true);

    // Tick to apply it
    const tick = await post("/api/tick");
    expect(tick.observations[0].hull).toBeLessThan(1.0);

    // Restore state
    await post("/api/restore", { tag: "e2e_pre_inject" });
  });

  test("snapshot → advance → restore returns to saved state", async () => {
    const snap = await post("/api/snapshot", { tag: "e2e_snap" });
    const savedTick = snap.tick;

    await post("/api/tick");
    await post("/api/tick");
    await post("/api/tick");

    const afterAdvance = await get("/api/status");
    expect(afterAdvance.tick).toBe(savedTick + 3);

    const restored = await post("/api/restore", { tag: "e2e_snap" });
    expect(restored.tick).toBe(savedTick);
  });

  test("auto tick loop runs multiple ticks", async () => {
    const { ws: dashWs, ticks } = await connectDashboard();

    tickLoop.start();
    await new Promise((r) => setTimeout(r, 500));
    tickLoop.stop();

    // Should have accumulated several ticks at 10/sec over 500ms
    expect(ticks.length).toBeGreaterThan(2);

    // Ticks should be sequential
    for (let i = 1; i < ticks.length; i++) {
      expect(ticks[i].tick).toBe(ticks[i - 1].tick + 1);
    }

    dashWs.close();
    await new Promise((r) => setTimeout(r, 100));
  });

  test("config change applies to simulation", async () => {
    const cfg = await post("/api/config", { event_freq_discovery: "0.99" });
    expect(cfg.ok).toBe(true);
    expect(cfg.entries).toBe(1);

    // Run several ticks — high discovery frequency should still not crash
    for (let i = 0; i < 10; i++) {
      const r = await post("/api/tick");
      expect(r.ok).toBe(true);
    }

    // Reset to normal
    await post("/api/config", { event_freq_discovery: "0.005" });
  });

  test("REST /api/probes matches simulation state", async () => {
    // Get state from both endpoints
    const status = await get("/api/status");
    const probes = await get("/api/probes");

    expect(probes.ok).toBe(true);
    expect(probes.probes.length).toBe(status.probes.length);

    // Each probe should match
    for (let i = 0; i < probes.probes.length; i++) {
      expect(probes.probes[i].id).toBe(status.probes[i].id);
      expect(probes.probes[i].name).toBe(status.probes[i].name);
      expect(probes.probes[i].status).toBe(status.probes[i].status);
    }

    // Single probe endpoint should also match
    const single = await get(`/api/probes/${status.probes[0].id}`);
    expect(single.ok).toBe(true);
    expect(single.probe.name).toBe(status.probes[0].name);
  });

  test("agent disconnect → probe gets fallback → agent reconnect", async () => {
    const status = await get("/api/status");
    const probeId = status.probes[0].id;

    // Connect, then disconnect
    const { ws: ws1 } = await connectAgent(probeId, () => ({ action: "wait" }));
    ws1.close();
    await new Promise((r) => setTimeout(r, 100));

    // Tick should work with fallback
    const resp = await post("/api/tick");
    expect(resp.ok).toBe(true);

    // Reconnect
    const { ws: ws2, received } = await connectAgent(probeId, () => ({
      action: "survey",
    }));
    expect(received[0].type).toBe("registered");

    ws2.close();
    await new Promise((r) => setTimeout(r, 100));
  });
});
