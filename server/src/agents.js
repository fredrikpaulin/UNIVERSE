/**
 * agents.js — Agent registry: maps probe_id strings to WebSocket connections.
 *
 * register(probeId, ws)              → associate ws with probe
 * unregister(probeId)                → remove agent
 * unregisterByWs(ws)                 → remove agent by ws ref
 * getAgent(probeId)                  → { ws, pendingResolve } or undefined
 * listAgents()                       → array of connected probe IDs
 * sendObservation(probeId, obs)      → send observation to agent's ws
 * waitForAction(probeId, timeoutMs)  → Promise<action | fallback>
 * resolveAction(probeId, action)     → deliver action from agent
 */

const agents = new Map();

const FALLBACK_ACTION = { action: "wait" };

export function register(probeId, ws) {
  const existing = agents.get(probeId);
  if (existing && existing.ws !== ws) {
    // Replace old connection
    try { existing.ws.close(); } catch (_) {}
  }
  agents.set(probeId, { ws, pendingResolve: null });
  ws._probeId = probeId;
}

export function unregister(probeId) {
  const agent = agents.get(probeId);
  if (agent && agent.pendingResolve) {
    agent.pendingResolve(FALLBACK_ACTION);
  }
  agents.delete(probeId);
}

export function unregisterByWs(ws) {
  if (ws._probeId) unregister(ws._probeId);
}

export function getAgent(probeId) {
  return agents.get(probeId);
}

export function listAgents() {
  return [...agents.keys()];
}

export function sendObservation(probeId, obs) {
  const agent = agents.get(probeId);
  if (!agent) return false;
  try {
    agent.ws.send(JSON.stringify({ type: "observe", ...obs }));
    return true;
  } catch (_) {
    unregister(probeId);
    return false;
  }
}

export function waitForAction(probeId, timeoutMs = 5000) {
  const agent = agents.get(probeId);
  if (!agent) return Promise.resolve(FALLBACK_ACTION);

  return new Promise((resolve) => {
    const timer = setTimeout(() => {
      if (agent.pendingResolve === resolve) agent.pendingResolve = null;
      resolve(FALLBACK_ACTION);
    }, timeoutMs);

    agent.pendingResolve = (action) => {
      clearTimeout(timer);
      agent.pendingResolve = null;
      resolve(action);
    };
  });
}

export function resolveAction(probeId, action) {
  const agent = agents.get(probeId);
  if (agent && agent.pendingResolve) {
    agent.pendingResolve(action);
  }
}

export function clear() {
  agents.clear();
}

export { FALLBACK_ACTION };
