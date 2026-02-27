import { test, expect, describe, beforeEach, afterEach } from "bun:test";
import { spawnSim, stopSim } from "../src/process.js";
import { createTickLoop } from "../src/tick.js";
import {
  register, unregister, resolveAction, listAgents, clear as clearAgents
} from "../src/agents.js";

/** Minimal mock WebSocket that captures sent messages. */
function mockWs() {
  const sent = [];
  return {
    sent,
    send(data) { sent.push(JSON.parse(data)); },
    close() {},
    _probeId: null,
  };
}

let sim;

beforeEach(async () => {
  clearAgents();
  sim = await spawnSim({ seed: 42 });
});

afterEach(async () => {
  if (sim) { await stopSim(sim); sim = null; }
});

describe("agents registry", () => {
  test("register and list agents", () => {
    const ws = mockWs();
    register("1-1", ws);
    expect(listAgents()).toEqual(["1-1"]);
  });

  test("unregister removes agent", () => {
    const ws = mockWs();
    register("1-1", ws);
    unregister("1-1");
    expect(listAgents()).toEqual([]);
  });

  test("replacing agent closes old ws", () => {
    const ws1 = mockWs();
    let closed = false;
    ws1.close = () => { closed = true; };
    register("1-1", ws1);
    const ws2 = mockWs();
    register("1-1", ws2);
    expect(closed).toBe(true);
    expect(listAgents()).toEqual(["1-1"]);
  });
});

describe("tick loop", () => {
  test("once() executes a single tick", async () => {
    const loop = createTickLoop({ sim, tickRate: 10, agentTimeout: 1000 });
    const resp = await loop.once();
    expect(resp.ok).toBe(true);
    expect(resp.tick).toBe(1);
    expect(resp.observations.length).toBe(1);
  });

  test("multiple once() calls advance tick", async () => {
    const loop = createTickLoop({ sim });
    await loop.once();
    await loop.once();
    const r3 = await loop.once();
    expect(r3.tick).toBe(3);
  });

  test("tick emits tick event", async () => {
    const loop = createTickLoop({ sim });
    const events = [];
    loop.on("tick", (e) => events.push(e));
    await loop.once();
    await loop.once();
    expect(events.length).toBe(2);
    expect(events[0].tick).toBe(1);
    expect(events[1].tick).toBe(2);
  });

  test("connected agent receives observation and action is applied", async () => {
    const loop = createTickLoop({ sim, agentTimeout: 2000 });

    // First tick — no agents, just get the initial probe_id
    const r1 = await loop.once();
    const probeId = r1.observations[0].probe_id;

    // Register a mock agent
    const ws = mockWs();
    register(probeId, ws);

    // Start a tick, and when the agent gets an observation, respond with survey
    const tickPromise = loop.once();

    // Wait a bit for the observation to be sent
    await new Promise((r) => setTimeout(r, 50));

    // Agent should have received an observation
    expect(ws.sent.length).toBeGreaterThan(0);
    expect(ws.sent[0].type).toBe("observe");

    // Agent responds with action
    resolveAction(probeId, { action: "survey" });

    const r2 = await tickPromise;
    expect(r2.ok).toBe(true);
    expect(r2.tick).toBe(2);
  });

  test("unresponsive agent gets fallback wait action", async () => {
    const loop = createTickLoop({ sim, agentTimeout: 100 });

    // First tick to get observations
    await loop.once();

    // Register agent that never responds
    const ws = mockWs();
    register("1-1", ws);

    // Tick should complete after timeout with fallback action
    const start = Date.now();
    const resp = await loop.once();
    const elapsed = Date.now() - start;

    expect(resp.ok).toBe(true);
    expect(elapsed).toBeGreaterThanOrEqual(90); // timeout ~100ms
    expect(resp.observations[0].status).toBe("active"); // probe still fine
  });

  test("start/stop runs ticks automatically", async () => {
    const loop = createTickLoop({ sim, tickRate: 50 }); // 50 ticks/sec
    const ticks = [];
    loop.on("tick", (e) => ticks.push(e.tick));

    loop.start();
    await new Promise((r) => setTimeout(r, 250));
    loop.stop();

    // Should have run several ticks in 250ms at 50/sec
    expect(ticks.length).toBeGreaterThan(3);
    // Ticks should be sequential
    for (let i = 1; i < ticks.length; i++) {
      expect(ticks[i]).toBe(ticks[i - 1] + 1);
    }
  });

  test("pause/resume halts and resumes ticking", async () => {
    const loop = createTickLoop({ sim, tickRate: 50 });
    const ticks = [];
    loop.on("tick", (e) => ticks.push(e.tick));

    loop.start();
    await new Promise((r) => setTimeout(r, 150));
    const countBeforePause = ticks.length;

    loop.pause();
    await new Promise((r) => setTimeout(r, 150));
    expect(ticks.length).toBe(countBeforePause); // no new ticks

    loop.resume();
    await new Promise((r) => setTimeout(r, 150));
    loop.stop();
    expect(ticks.length).toBeGreaterThan(countBeforePause);
  });

  test("state reflects current loop status", async () => {
    const loop = createTickLoop({ sim });
    expect(loop.state.running).toBe(false);
    expect(loop.state.paused).toBe(false);

    loop.start();
    expect(loop.state.running).toBe(true);

    loop.pause();
    expect(loop.state.paused).toBe(true);

    loop.resume();
    expect(loop.state.paused).toBe(false);

    loop.stop();
    expect(loop.state.running).toBe(false);
  });
});
