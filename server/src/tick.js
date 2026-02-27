/**
 * tick.js — Tick coordinator with agent synchronization.
 *
 * createTickLoop({ sim, tickRate, agentTimeout })
 *   → { start(), stop(), pause(), resume(), once(), on(event, fn), state }
 *
 * Each tick:
 *  1. Send observations from last tick to connected agents
 *  2. Wait for agent actions (with timeout → fallback)
 *  3. Build actions object, send tick command to sim
 *  4. Store observations for next iteration
 *  5. Emit "tick" event for dashboard subscribers
 */

import { sendCommand } from "./process.js";
import {
  listAgents, sendObservation, waitForAction, FALLBACK_ACTION
} from "./agents.js";

export function createTickLoop({ sim, tickRate = 10, agentTimeout = 5000 } = {}) {
  let timer = null;
  let running = false;
  let paused = false;
  let lastObservations = null; // map: probeId → observation
  let tickCount = 0;
  const listeners = { tick: [], error: [] };

  function emit(event, data) {
    for (const fn of listeners[event] || []) {
      try { fn(data); } catch (_) {}
    }
  }

  async function executeTick() {
    // 1. Send observations to connected agents
    const connectedAgents = listAgents();
    if (lastObservations) {
      for (const probeId of connectedAgents) {
        const obs = lastObservations.get(probeId);
        if (obs) sendObservation(probeId, obs);
      }
    }

    // 2. Wait for actions from all connected agents
    const actionPromises = connectedAgents.map(async (probeId) => {
      const action = await waitForAction(probeId, agentTimeout);
      return { probeId, action };
    });
    const results = await Promise.allSettled(actionPromises);

    // 3. Build actions object
    const actions = {};
    for (const result of results) {
      if (result.status === "fulfilled") {
        const { probeId, action } = result.value;
        actions[probeId] = action || FALLBACK_ACTION;
      }
    }

    // 4. Send tick to sim
    const resp = await sendCommand(sim, { cmd: "tick", actions });
    if (!resp.ok) {
      emit("error", { tick: tickCount, error: resp.error });
      return resp;
    }
    tickCount = resp.tick;

    // 5. Index observations by probe_id for next iteration
    lastObservations = new Map();
    for (const obs of resp.observations || []) {
      lastObservations.set(obs.probe_id, obs);
    }

    // 6. Emit tick event
    emit("tick", {
      tick: resp.tick,
      observations: resp.observations,
      agents: { connected: connectedAgents.length }
    });

    return resp;
  }

  function scheduleNext() {
    if (!running || paused || tickRate <= 0) return;
    const interval = Math.max(1, Math.round(1000 / tickRate));
    timer = setTimeout(async () => {
      try { await executeTick(); } catch (e) { emit("error", e); }
      scheduleNext();
    }, interval);
  }

  return {
    start() {
      if (running) return;
      running = true;
      paused = false;
      scheduleNext();
    },

    stop() {
      running = false;
      paused = false;
      if (timer) { clearTimeout(timer); timer = null; }
    },

    pause() { paused = true; if (timer) { clearTimeout(timer); timer = null; } },
    resume() { if (paused) { paused = false; if (running) scheduleNext(); } },

    /** Execute exactly one tick (works even when stopped/paused). */
    async once() { return executeTick(); },

    on(event, fn) { (listeners[event] = listeners[event] || []).push(fn); },

    get state() {
      return { running, paused, tick: tickCount, agents: listAgents().length };
    }
  };
}
