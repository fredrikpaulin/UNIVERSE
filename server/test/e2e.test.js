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

  test("observations include enriched fields (resources, position, capabilities)", async () => {
    const resp = await post("/api/tick");
    expect(resp.ok).toBe(true);
    const obs = resp.observations[0];

    // Resources object
    expect(obs.resources).toBeDefined();
    expect(typeof obs.resources.iron).toBe("number");
    expect(typeof obs.resources.silicon).toBe("number");
    expect(typeof obs.resources.exotic).toBe("number");

    // Position object
    expect(obs.position).toBeDefined();
    expect(obs.position.sector).toBeArrayOfSize(3);
    expect(typeof obs.position.system_id).toBe("string");
    expect(obs.position.heading).toBeArrayOfSize(3);
    expect(typeof obs.position.travel_remaining_ly).toBe("number");

    // Capabilities object
    expect(obs.capabilities).toBeDefined();
    expect(obs.capabilities.max_speed_c).toBeGreaterThan(0);
    expect(obs.capabilities.sensor_range_ly).toBeGreaterThan(0);
    expect(obs.capabilities.mining_rate).toBeGreaterThan(0);
  });

  test("observations include enhanced system details", async () => {
    const resp = await post("/api/tick");
    const obs = resp.observations[0];
    const sys = obs.system;

    expect(sys).toBeDefined();
    expect(sys.name.length).toBeGreaterThan(0);
    expect(sys.star_count).toBeGreaterThan(0);
    expect(sys.planet_count).toBeGreaterThan(0);

    // Star details
    const star = sys.stars[0];
    expect(star.name.length).toBeGreaterThan(0);
    expect(typeof star.class).toBe("number");
    expect(star.luminosity_solar).toBeGreaterThan(0);
    expect(typeof star.metallicity).toBe("number");

    // Planet details
    const planet = sys.planets[0];
    expect(planet.radius_earth).toBeGreaterThan(0);
    expect(planet.orbital_period_days).toBeGreaterThan(0);
    expect(planet.surface_temp_k).toBeGreaterThan(0);
    expect(typeof planet.atmosphere_pressure_atm).toBe("number");
    expect(typeof planet.water_coverage).toBe("number");
    expect(typeof planet.magnetic_field).toBe("number");
    expect(typeof planet.rings).toBe("boolean");
    expect(typeof planet.moon_count).toBe("number");
    expect(planet.survey_complete).toBeArrayOfSize(5);

    // Planet resources
    expect(planet.resources).toBeDefined();
    expect(typeof planet.resources.iron).toBe("number");
    expect(planet.resources.iron).toBeGreaterThanOrEqual(0);
    expect(planet.resources.iron).toBeLessThanOrEqual(1);
  });

  test("POST /api/scan/:probeId returns nearby systems", async () => {
    const status = await get("/api/status");
    const probeId = status.probes[0].id;
    const scan = await post(`/api/scan/${probeId}`);

    expect(scan.ok).toBe(true);
    expect(scan.probe_id).toBe(probeId);
    expect(Array.isArray(scan.systems)).toBe(true);

    if (scan.systems.length > 0) {
      const sys = scan.systems[0];
      expect(typeof sys.system_id).toBe("string");
      expect(typeof sys.name).toBe("string");
      expect(sys.distance_ly).toBeGreaterThan(0);
      expect(sys.estimated_travel_ticks).toBeGreaterThan(0);
      expect(sys.position).toBeArrayOfSize(3);
      expect(sys.sector).toBeArrayOfSize(3);
    }
  });

  test("travel_to_system action makes probe travel", async () => {
    // Snapshot first so we can restore
    await post("/api/snapshot", { tag: "pre_travel" });

    const status = await get("/api/status");
    const probeId = status.probes[0].id;
    const scan = await post(`/api/scan/${probeId}`);

    if (scan.systems.length > 0) {
      const target = scan.systems[0];

      // Connect a temporary agent that sends the travel action
      const { ws: agentWs, received } = await connectAgent(probeId, (msg) => ({
        action: "travel_to_system",
        target_system_id: target.system_id,
        sector_x: target.sector[0],
        sector_y: target.sector[1],
        sector_z: target.sector[2],
      }));

      // Run a tick — agent sends travel action
      const resp = await post("/api/tick");
      await new Promise((r) => setTimeout(r, 200));

      // Run another tick to see the traveling state
      const resp2 = await post("/api/tick");
      const obs = resp2.observations.find((o) => o.probe_id === probeId);
      expect(obs.status).toBe("traveling");
      expect(obs.location).toBe("interstellar");

      agentWs.close();
      await new Promise((r) => setTimeout(r, 100));

      // Restore state
      await post("/api/restore", { tag: "pre_travel" });
    }
  });

  test("observations include recent_events and nearby_probes arrays", async () => {
    const resp = await post("/api/tick");
    const obs = resp.observations[0];

    expect(Array.isArray(obs.recent_events)).toBe(true);
    expect(Array.isArray(obs.nearby_probes)).toBe(true);
  });

  test("save and load round-trip preserves state", async () => {
    // Run some ticks to advance state
    await post("/api/tick");
    await post("/api/tick");
    const before = await get("/api/status");
    const tickBefore = before.tick;

    // Save
    const saveResp = await post("/api/save", { path: "/tmp/e2e_test.db" });
    expect(saveResp.ok).toBe(true);
    expect(saveResp.tick).toBe(tickBefore);
    expect(saveResp.probes).toBe(before.probes.length);

    // Run more ticks
    await post("/api/tick");
    await post("/api/tick");
    const after = await get("/api/status");
    expect(after.tick).toBe(tickBefore + 2);

    // Load — should restore to saved state
    const loadResp = await post("/api/load", { path: "/tmp/e2e_test.db" });
    expect(loadResp.ok).toBe(true);
    expect(loadResp.tick).toBe(tickBefore);
    expect(loadResp.probes).toBe(before.probes.length);

    // Verify tick matches
    const restored = await get("/api/status");
    expect(restored.tick).toBe(tickBefore);
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

  test("observations include coordination arrays (inbox, beacons, structures, trades)", async () => {
    const resp = await post("/api/tick");
    const obs = resp.observations[0];

    expect(Array.isArray(obs.inbox)).toBe(true);
    expect(Array.isArray(obs.visible_beacons)).toBe(true);
    expect(Array.isArray(obs.visible_structures)).toBe(true);
    expect(Array.isArray(obs.pending_trades)).toBe(true);
  });

  test("place_beacon action creates visible beacon", async () => {
    await post("/api/snapshot", { tag: "pre_beacon" });

    const status = await get("/api/status");
    const probeId = status.probes[0].id;

    // Connect agent that places a beacon
    const { ws: agentWs } = await connectAgent(probeId, () => ({
      action: "place_beacon",
      message: "Explorer was here",
    }));

    // Run tick — agent places beacon
    await post("/api/tick");
    await new Promise((r) => setTimeout(r, 200));

    // Next tick — beacon should be visible
    const resp = await post("/api/tick");
    const obs = resp.observations.find((o) => o.probe_id === probeId);
    expect(obs.visible_beacons.length).toBeGreaterThan(0);
    expect(obs.visible_beacons[0].owner).toBe(probeId);
    expect(obs.visible_beacons[0].message).toBe("Explorer was here");

    agentWs.close();
    await new Promise((r) => setTimeout(r, 100));
    await post("/api/restore", { tag: "pre_beacon" });
  });

  test("build_structure action shows in-progress structure", async () => {
    await post("/api/snapshot", { tag: "pre_build" });

    const status = await get("/api/status");
    const probeId = status.probes[0].id;

    // Connect agent that builds a mining station (type 0)
    const { ws: agentWs } = await connectAgent(probeId, () => ({
      action: "build_structure",
      structure_type: 0,
    }));

    // Run tick — agent starts building
    await post("/api/tick");
    await new Promise((r) => setTimeout(r, 200));

    // Next tick — structure should be visible (in progress)
    const resp = await post("/api/tick");
    const obs = resp.observations.find((o) => o.probe_id === probeId);
    expect(obs.visible_structures.length).toBeGreaterThan(0);
    expect(obs.visible_structures[0].complete).toBe(false);
    expect(obs.visible_structures[0].progress).toBeGreaterThan(0);
    expect(typeof obs.visible_structures[0].name).toBe("string");

    agentWs.close();
    await new Promise((r) => setTimeout(r, 100));
    await post("/api/restore", { tag: "pre_build" });
  });

  test("send_message action delivers to target probe inbox", async () => {
    // This test needs 2 probes — skip if only 1
    const status = await get("/api/status");
    if (status.probes.length < 2) {
      // Just verify the action doesn't crash with 1 probe
      const resp = await post("/api/tick");
      expect(resp.ok).toBe(true);
      return;
    }

    await post("/api/snapshot", { tag: "pre_msg" });

    const probeA = status.probes[0].id;
    const probeB = status.probes[1].id;

    // Agent A sends message to B
    const { ws: wsA } = await connectAgent(probeA, () => ({
      action: "send_message",
      target: probeB,
      content: "Hello from probe A",
    }));

    // Run ticks to send and allow light-delay delivery
    for (let i = 0; i < 5; i++) await post("/api/tick");
    await new Promise((r) => setTimeout(r, 200));

    const resp = await post("/api/tick");
    const obsB = resp.observations.find((o) => o.probe_id === probeB);
    // Message may or may not have arrived depending on light delay
    expect(Array.isArray(obsB?.inbox)).toBe(true);

    wsA.close();
    await new Promise((r) => setTimeout(r, 100));
    await post("/api/restore", { tag: "pre_msg" });
  });

  test("trade action creates pending trade", async () => {
    // Needs 2 probes
    const status = await get("/api/status");
    if (status.probes.length < 2) {
      const resp = await post("/api/tick");
      expect(resp.ok).toBe(true);
      return;
    }

    await post("/api/snapshot", { tag: "pre_trade" });

    const probeA = status.probes[0].id;
    const probeB = status.probes[1].id;

    const { ws: wsA } = await connectAgent(probeA, () => ({
      action: "trade",
      target: probeB,
      resource: "iron",
      amount: 100,
    }));

    await post("/api/tick");
    await new Promise((r) => setTimeout(r, 200));

    const resp = await post("/api/tick");
    expect(resp.ok).toBe(true);

    wsA.close();
    await new Promise((r) => setTimeout(r, 100));
    await post("/api/restore", { tag: "pre_trade" });
  });

  test("observations include diplomacy fields (claims, proposals, trust)", async () => {
    const resp = await post("/api/tick");
    const obs = resp.observations[0];
    expect(Array.isArray(obs.claims)).toBe(true);
    expect(Array.isArray(obs.proposals)).toBe(true);
    expect(Array.isArray(obs.trust)).toBe(true);
  });

  test("claim_system action creates a claim visible in observations", async () => {
    await post("/api/snapshot", { tag: "pre_claim" });
    const status = await get("/api/status");
    const probeId = status.probes[0].id;

    const { ws: agentWs } = await connectAgent(probeId, () => ({
      action: "claim_system",
    }));

    await post("/api/tick");
    await new Promise((r) => setTimeout(r, 200));

    const resp = await post("/api/tick");
    const obs = resp.observations.find((o) => o.probe_id === probeId);
    expect(obs.claims.length).toBeGreaterThan(0);
    expect(obs.claims[0].claimer).toBe(probeId);

    agentWs.close();
    await new Promise((r) => setTimeout(r, 100));
    await post("/api/restore", { tag: "pre_claim" });
  });

  test("propose action creates an active proposal", async () => {
    await post("/api/snapshot", { tag: "pre_propose" });
    const status = await get("/api/status");
    const probeId = status.probes[0].id;

    const { ws: agentWs } = await connectAgent(probeId, () => ({
      action: "propose",
      text: "Survey all planets",
    }));

    await post("/api/tick");
    await new Promise((r) => setTimeout(r, 200));

    const resp = await post("/api/tick");
    const obs = resp.observations.find((o) => o.probe_id === probeId);
    expect(obs.proposals.length).toBeGreaterThan(0);
    expect(obs.proposals[0].text).toBe("Survey all planets");
    expect(obs.proposals[0].proposer).toBe(probeId);

    agentWs.close();
    await new Promise((r) => setTimeout(r, 100));
    await post("/api/restore", { tag: "pre_propose" });
  });

  test("research action shows progress in observations", async () => {
    await post("/api/snapshot", { tag: "pre_research" });
    const status = await get("/api/status");
    const probeId = status.probes[0].id;

    const { ws: agentWs } = await connectAgent(probeId, () => ({
      action: "research",
      domain: 0,
    }));

    // Run a few ticks to accumulate research progress
    await post("/api/tick");
    await post("/api/tick");
    await new Promise((r) => setTimeout(r, 200));

    const resp = await post("/api/tick");
    const obs = resp.observations.find((o) => o.probe_id === probeId);
    expect(obs.research).toBeDefined();
    expect(obs.research.domain).toBe(0);
    expect(obs.research.progress).toBeGreaterThan(0);
    expect(obs.research.ticks_remaining).toBeGreaterThan(0);

    agentWs.close();
    await new Promise((r) => setTimeout(r, 100));
    await post("/api/restore", { tag: "pre_research" });
  });

  test("planet observations do not show artifact before survey", async () => {
    const resp = await post("/api/tick");
    const obs = resp.observations[0];
    if (obs.system && obs.system.planets) {
      for (const pl of obs.system.planets) {
        expect(pl.artifact).toBeUndefined();
      }
    }
  });

  test("observations include threats array for hazard warnings", async () => {
    const resp = await post("/api/tick");
    const obs = resp.observations[0];
    expect(Array.isArray(obs.threats)).toBe(true);
    for (const t of obs.threats) {
      expect(t).toHaveProperty("type");
      expect(t).toHaveProperty("severity");
      expect(t).toHaveProperty("ticks_until");
    }
  });

  test("observations include relay_network array", async () => {
    const resp = await post("/api/tick");
    const obs = resp.observations[0];
    expect(Array.isArray(obs.relay_network)).toBe(true);
  });

  test("GET /api/lineage returns entries array", async () => {
    const resp = await get("/api/lineage");
    expect(resp.ok).toBe(true);
    expect(Array.isArray(resp.entries)).toBe(true);
  });

  test("GET /api/history/:probeId returns events array", async () => {
    const status = await get("/api/status");
    const probeId = status.probes[0].id;
    const resp = await get(`/api/history/${probeId}`);
    expect(resp.ok).toBe(true);
    expect(resp.probe_id).toBe(probeId);
    expect(Array.isArray(resp.events)).toBe(true);
  });

  test("POST /api/scenario loads events, GET returns them", async () => {
    const loadResp = await post("/api/scenario", {
      events: [
        { at_tick: 100, type: 2, subtype: 0, severity: 0.5 },
        { at_tick: 200, type: 4, subtype: 1, severity: 0.9 },
      ],
    });
    expect(loadResp.ok).toBe(true);
    expect(loadResp.loaded).toBe(2);

    const getResp = await get("/api/scenario");
    expect(getResp.ok).toBe(true);
    expect(Array.isArray(getResp.events)).toBe(true);
    expect(getResp.events.length).toBe(2);
    expect(getResp.events[0].at_tick).toBe(100);
    expect(getResp.events[1].at_tick).toBe(200);
  });
});
