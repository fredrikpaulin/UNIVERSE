/**
 * dashboard.js — Dashboard subscriber management.
 *
 * Maintains a set of WebSocket clients that receive tick broadcasts.
 * No registration needed — just connect to /ws/dashboard.
 */

const clients = new Set();

export function addClient(ws) {
  clients.add(ws);
}

export function removeClient(ws) {
  clients.delete(ws);
}

export function broadcast(tickEvent) {
  const msg = JSON.stringify({ type: "tick", ...tickEvent });
  for (const ws of clients) {
    try { ws.send(msg); }
    catch (_) { clients.delete(ws); }
  }
}

export function clientCount() {
  return clients.size;
}

export function clear() {
  clients.clear();
}
