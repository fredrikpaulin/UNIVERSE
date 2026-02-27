import { test, expect, describe, beforeAll, afterAll } from "bun:test";
import { spawnSim, stopSim } from "../src/process.js";
import { createTickLoop } from "../src/tick.js";
import { handleAPI } from "../src/api.js";
import {
  register, unregisterByWs, resolveAction, clear as clearAgents
} from "../src/agents.js";

let sim, tickLoop, server;
let port;

beforeAll(async () => {
  clearAgents();
  sim = await spawnSim({ seed: 42 });
  tickLoop = createTickLoop({ sim, tickRate: 10, agentTimeout: 1000 });

  // Start a real Bun server for integration tests
  const dashboardClients = new Set();

  tickLoop.on("tick", (e) => {
    for (const ws of dashboardClients) {
      try { ws.send(JSON.stringify({ type: "tick", ...e })); }
      catch (_) { dashboardClients.delete(ws); }
    }
  });

  server = Bun.serve({
    port: 0, // random available port
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
      open(ws) { if (ws.data?.type === "dashboard") dashboardClients.add(ws); },
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
        if (ws.data?.type === "dashboard") dashboardClients.delete(ws);
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
  if (sim) await stopSim(sim);
});

const get = (path) => fetch(`http://localhost:${port}${path}`).then((r) => r.json());
const post = (path, body) =>
  fetch(`http://localhost:${port}${path}`, {
    method: "POST",
    headers: { "Content-Type": "application/json" },
    body: JSON.stringify(body),
  }).then((r) => r.json());

describe("REST API", () => {
  test("GET /api/status", async () => {
    const r = await get("/api/status");
    expect(r.ok).toBe(true);
    expect(r.probes.length).toBeGreaterThan(0);
    expect(r.probes[0].name).toBe("Bob");
  });

  test("GET /api/probes", async () => {
    const r = await get("/api/probes");
    expect(r.ok).toBe(true);
    expect(r.probes[0].name).toBe("Bob");
  });

  test("GET /api/probes/:id", async () => {
    const status = await get("/api/status");
    const id = status.probes[0].id;
    const r = await get(`/api/probes/${id}`);
    expect(r.ok).toBe(true);
    expect(r.probe.name).toBe("Bob");
  });

  test("GET /api/probes/:id — not found", async () => {
    const r = await get("/api/probes/99-99");
    expect(r.ok).toBe(false);
    expect(r.error).toContain("not found");
  });

  test("POST /api/tick — manual tick", async () => {
    const r = await post("/api/tick");
    expect(r.ok).toBe(true);
    expect(r.tick).toBeGreaterThan(0);
    expect(r.observations.length).toBe(1);
  });

  test("GET /api/metrics", async () => {
    const r = await get("/api/metrics");
    expect(r.ok).toBe(true);
    expect(typeof r.avg_tech).toBe("number");
  });

  test("POST /api/inject", async () => {
    const r = await post("/api/inject", {
      type: "hazard",
      subtype: 0,
      severity: 0.5,
    });
    expect(r.ok).toBe(true);
    expect(r.queued).toBeGreaterThan(0);
  });

  test("POST /api/snapshot + POST /api/restore", async () => {
    const snap = await post("/api/snapshot", { tag: "api_test" });
    expect(snap.ok).toBe(true);
    expect(snap.snapshot).toBe("api_test");
    const saveTick = snap.tick;

    // Advance a few ticks
    await post("/api/tick");
    await post("/api/tick");

    const restore = await post("/api/restore", { tag: "api_test" });
    expect(restore.ok).toBe(true);
    expect(restore.tick).toBe(saveTick);
  });

  test("POST /api/config", async () => {
    const r = await post("/api/config", { event_freq_discovery: "0.01" });
    expect(r.ok).toBe(true);
    expect(r.entries).toBe(1);
  });

  test("GET /api/agents — empty initially", async () => {
    const r = await get("/api/agents");
    expect(r.ok).toBe(true);
    expect(Array.isArray(r.agents)).toBe(true);
  });

  test("POST /api/pause + POST /api/resume", async () => {
    const p = await post("/api/pause");
    expect(p.ok).toBe(true);
    expect(p.state.paused).toBe(true);

    const r = await post("/api/resume");
    expect(r.ok).toBe(true);
    expect(r.state.paused).toBe(false);
  });

  test("GET /api/state", async () => {
    const r = await get("/api/state");
    expect(r.ok).toBe(true);
    expect(typeof r.state.tick).toBe("number");
    expect(typeof r.state.running).toBe("boolean");
  });

  test("unknown route returns 404", async () => {
    const r = await fetch(`http://localhost:${port}/api/nope`);
    expect(r.status).toBe(404);
    const body = await r.json();
    expect(body.ok).toBe(false);
  });
});

describe("WebSocket agent", () => {
  test("agent connects, registers, receives confirmation", async () => {
    const ws = new WebSocket(`ws://localhost:${port}/ws`);
    const messages = [];

    await new Promise((resolve, reject) => {
      ws.onopen = () => {
        ws.send(JSON.stringify({ type: "register", probe_id: "1-1" }));
      };
      ws.onmessage = (e) => {
        messages.push(JSON.parse(e.data));
        if (messages.length === 1) resolve();
      };
      ws.onerror = reject;
      setTimeout(reject, 3000);
    });

    expect(messages[0].type).toBe("registered");
    expect(messages[0].probe_id).toBe("1-1");

    // Verify agent shows up in registry
    const agents = await get("/api/agents");
    expect(agents.agents).toContain("1-1");

    ws.close();
    await new Promise((r) => setTimeout(r, 100));
  });

  test("dashboard ws receives tick broadcasts", async () => {
    const ws = new WebSocket(`ws://localhost:${port}/ws/dashboard`);
    const messages = [];

    await new Promise((resolve) => {
      ws.onopen = resolve;
      setTimeout(resolve, 1000);
    });

    ws.onmessage = (e) => messages.push(JSON.parse(e.data));

    // Trigger a tick via REST
    await post("/api/tick");
    await new Promise((r) => setTimeout(r, 200));

    expect(messages.length).toBeGreaterThan(0);
    expect(messages[0].type).toBe("tick");
    expect(typeof messages[0].tick).toBe("number");

    ws.close();
    await new Promise((r) => setTimeout(r, 100));
  });
});
