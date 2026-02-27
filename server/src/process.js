/**
 * process.js — Spawn C simulation, communicate via JSON pipes.
 *
 * spawnSim({ seed, simPath })  → sim handle with .send() helper
 * sendCommand(sim, cmd)        → parsed JSON response
 * stopSim(sim, timeoutMs?)     → clean shutdown
 */

import { resolve, dirname } from "node:path";
import { createLineReader, writeLine } from "./protocol.js";

const DEFAULT_SIM_PATH = resolve(import.meta.dir, "../../sim/build/universe");

export async function spawnSim({ seed = 42, simPath = DEFAULT_SIM_PATH } = {}) {
  const proc = Bun.spawn([simPath, "--pipe", "--seed", String(seed)], {
    stdin: "pipe",
    stdout: "pipe",
    stderr: "pipe",
    env: { ...process.env, LD_LIBRARY_PATH: resolve(dirname(simPath), "..") },
  });

  const reader = createLineReader(proc.stdout);

  // Wait for ready message
  const readyMsg = await reader.next();
  if (!readyMsg || !readyMsg.ok || !readyMsg.ready) {
    proc.kill();
    throw new Error("sim failed to start");
  }

  const sim = { proc, reader, readyMsg };
  sim.send = (cmd) => sendCommand(sim, cmd);
  return sim;
}

export async function sendCommand(sim, cmd) {
  writeLine(sim.proc.stdin, cmd);
  await sim.proc.stdin.flush();
  const resp = await sim.reader.next();
  if (resp === null) throw new Error("sim closed unexpectedly");
  return resp;
}

export async function stopSim(sim, timeoutMs = 3000) {
  try {
    writeLine(sim.proc.stdin, { cmd: "quit" });
    await sim.proc.stdin.flush();
    sim.proc.stdin.end();
  } catch (_) { /* stdin may already be closed */ }

  const timeout = new Promise((r) => setTimeout(() => {
    sim.proc.kill();
    r();
  }, timeoutMs));

  await Promise.race([sim.proc.exited, timeout]);
}
