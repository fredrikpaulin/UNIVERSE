import { test, expect, describe, afterEach } from "bun:test";
import { spawnSim, sendCommand, stopSim } from "../src/process.js";

let sim;

afterEach(async () => {
  if (sim) { await stopSim(sim); sim = null; }
});

describe("spawnSim", () => {
  test("starts and returns ready message", async () => {
    sim = await spawnSim({ seed: 42 });
    expect(sim.readyMsg.ok).toBe(true);
    expect(sim.readyMsg.ready).toBe(true);
    expect(sim.readyMsg.seed).toBe(42);
    expect(sim.readyMsg.tick).toBe(0);
  });

  test("accepts custom seed", async () => {
    sim = await spawnSim({ seed: 999 });
    expect(sim.readyMsg.seed).toBe(999);
  });
});

describe("sendCommand", () => {
  test("status returns probe list", async () => {
    sim = await spawnSim({ seed: 42 });
    const res = await sim.send({ cmd: "status" });
    expect(res.ok).toBe(true);
    expect(res.probes.length).toBeGreaterThan(0);
    expect(res.probes[0].name).toBe("Bob");
  });

  test("tick advances and returns observations", async () => {
    sim = await spawnSim({ seed: 42 });
    const r1 = await sim.send({ cmd: "tick", actions: {} });
    expect(r1.ok).toBe(true);
    expect(r1.tick).toBe(1);
    expect(r1.observations.length).toBe(1);
    expect(r1.observations[0].name).toBe("Bob");

    const r2 = await sim.send({ cmd: "tick", actions: {} });
    expect(r2.tick).toBe(2);
  });

  test("tick with action applies to probe", async () => {
    sim = await spawnSim({ seed: 42 });
    const status = await sim.send({ cmd: "status" });
    const probeId = status.probes[0].id;

    const r = await sim.send({
      cmd: "tick",
      actions: { [probeId]: { action: "survey" } }
    });
    expect(r.ok).toBe(true);
    expect(r.observations[0].probe_id).toBe(probeId);
  });

  test("metrics returns simulation metrics", async () => {
    sim = await spawnSim({ seed: 42 });
    // Run a few ticks first
    for (let i = 0; i < 10; i++) await sim.send({ cmd: "tick", actions: {} });
    const m = await sim.send({ cmd: "metrics" });
    expect(m.ok).toBe(true);
    expect(m.tick).toBe(10);
    expect(m.probes_spawned).toBeGreaterThan(0);
  });

  test("inject queues event and affects next tick", async () => {
    sim = await spawnSim({ seed: 42 });
    const inj = await sim.send({
      cmd: "inject",
      event: { type: "hazard", subtype: 0, severity: 0.8 }
    });
    expect(inj.ok).toBe(true);
    expect(inj.queued).toBe(1);

    const tick = await sim.send({ cmd: "tick", actions: {} });
    expect(tick.observations[0].hull).toBeLessThan(1.0);
  });

  test("snapshot and restore", async () => {
    sim = await spawnSim({ seed: 42 });
    await sim.send({ cmd: "tick", actions: {} });
    await sim.send({ cmd: "tick", actions: {} });

    const snap = await sim.send({ cmd: "snapshot", tag: "cp1" });
    expect(snap.ok).toBe(true);
    expect(snap.tick).toBe(2);

    await sim.send({ cmd: "tick", actions: {} });
    await sim.send({ cmd: "tick", actions: {} });
    const status4 = await sim.send({ cmd: "status" });
    expect(status4.tick).toBe(4);

    const restored = await sim.send({ cmd: "restore", tag: "cp1" });
    expect(restored.ok).toBe(true);
    expect(restored.tick).toBe(2);

    const status2 = await sim.send({ cmd: "status" });
    expect(status2.tick).toBe(2);
  });

  test("restore nonexistent snapshot returns error", async () => {
    sim = await spawnSim({ seed: 42 });
    const res = await sim.send({ cmd: "restore", tag: "nope" });
    expect(res.ok).toBe(false);
    expect(res.error).toContain("not found");
  });

  test("config sets values", async () => {
    sim = await spawnSim({ seed: 42 });
    const res = await sim.send({
      cmd: "config",
      data: { event_freq_discovery: "0.01", mutation_rate: "0.15" }
    });
    expect(res.ok).toBe(true);
    expect(res.entries).toBe(2);
  });

  test("unknown command returns error", async () => {
    sim = await spawnSim({ seed: 42 });
    const res = await sim.send({ cmd: "bogus" });
    expect(res.ok).toBe(false);
    expect(res.error).toContain("unknown");
  });

  test("100 ticks stress test", async () => {
    sim = await spawnSim({ seed: 42 });
    for (let i = 0; i < 100; i++) {
      const r = await sim.send({ cmd: "tick", actions: {} });
      expect(r.tick).toBe(i + 1);
    }
    const status = await sim.send({ cmd: "status" });
    expect(status.tick).toBe(100);
    expect(status.probes[0].status).toBe("active");
  });
});

describe("stopSim", () => {
  test("clean shutdown", async () => {
    sim = await spawnSim({ seed: 42 });
    await stopSim(sim);
    sim = null; // prevent afterEach double-stop
  });
});
