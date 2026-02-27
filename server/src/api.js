/**
 * api.js — REST route handlers. Pure functions, no framework.
 *
 * handleAPI(url, req, { sim, tickLoop }) → Response
 */

import { sendCommand } from "./process.js";
import { listAgents } from "./agents.js";

const json = (data, status = 200) =>
  new Response(JSON.stringify(data), {
    status,
    headers: { "Content-Type": "application/json" },
  });

export async function handleAPI(url, req, { sim, tickLoop }) {
  const method = req.method;
  const path = url.pathname;

  // GET /api/status
  if (method === "GET" && path === "/api/status") {
    return json(await sendCommand(sim, { cmd: "status" }));
  }

  // GET /api/metrics
  if (method === "GET" && path === "/api/metrics") {
    return json(await sendCommand(sim, { cmd: "metrics" }));
  }

  // GET /api/probes
  if (method === "GET" && path === "/api/probes") {
    const status = await sendCommand(sim, { cmd: "status" });
    return json({ ok: true, probes: status.probes });
  }

  // GET /api/probes/:id
  if (method === "GET" && path.startsWith("/api/probes/")) {
    const id = path.slice("/api/probes/".length);
    const status = await sendCommand(sim, { cmd: "status" });
    const probe = status.probes?.find((p) => p.id === id);
    if (!probe) return json({ ok: false, error: "probe not found" }, 404);
    return json({ ok: true, probe });
  }

  // POST /api/tick — manual single tick
  if (method === "POST" && path === "/api/tick") {
    const resp = await tickLoop.once();
    return json(resp);
  }

  // POST /api/inject
  if (method === "POST" && path === "/api/inject") {
    const body = await req.json();
    return json(await sendCommand(sim, { cmd: "inject", event: body }));
  }

  // POST /api/snapshot
  if (method === "POST" && path === "/api/snapshot") {
    const body = await req.json();
    return json(await sendCommand(sim, { cmd: "snapshot", tag: body.tag }));
  }

  // POST /api/restore
  if (method === "POST" && path === "/api/restore") {
    const body = await req.json();
    return json(await sendCommand(sim, { cmd: "restore", tag: body.tag }));
  }

  // POST /api/config
  if (method === "POST" && path === "/api/config") {
    const body = await req.json();
    return json(await sendCommand(sim, { cmd: "config", data: body }));
  }

  // GET /api/agents
  if (method === "GET" && path === "/api/agents") {
    return json({ ok: true, agents: listAgents() });
  }

  // POST /api/pause
  if (method === "POST" && path === "/api/pause") {
    tickLoop.pause();
    return json({ ok: true, state: tickLoop.state });
  }

  // POST /api/resume
  if (method === "POST" && path === "/api/resume") {
    tickLoop.resume();
    return json({ ok: true, state: tickLoop.state });
  }

  // GET /api/state — tick loop state
  if (method === "GET" && path === "/api/state") {
    return json({ ok: true, state: tickLoop.state });
  }

  // POST /api/scan/:probeId — long-range sensor scan
  if (method === "POST" && path.startsWith("/api/scan/")) {
    const probeId = path.slice("/api/scan/".length);
    return json(await sendCommand(sim, { cmd: "scan", probe_id: probeId }));
  }

  // POST /api/save
  if (method === "POST" && path === "/api/save") {
    const body = await req.json();
    const savePath = body.path || "universe.db";
    return json(await sendCommand(sim, { cmd: "save", path: savePath }));
  }

  // POST /api/load
  if (method === "POST" && path === "/api/load") {
    const body = await req.json();
    const loadPath = body.path || "universe.db";
    return json(await sendCommand(sim, { cmd: "load", path: loadPath }));
  }

  // GET /api/tick-rate
  if (method === "GET" && path === "/api/tick-rate") {
    return json({ ok: true, tickRate: tickLoop.tickRate });
  }

  // POST /api/tick-rate
  if (method === "POST" && path === "/api/tick-rate") {
    const body = await req.json();
    if (typeof body.tickRate === "number" && body.tickRate > 0) {
      tickLoop.tickRate = body.tickRate;
      return json({ ok: true, tickRate: tickLoop.tickRate });
    }
    return json({ ok: false, error: "invalid tickRate" }, 400);
  }

  // POST /api/scenario — load scenario events
  if (method === "POST" && path === "/api/scenario") {
    const body = await req.json();
    return json(await sendCommand(sim, { cmd: "scenario", events: body.events }));
  }

  // GET /api/scenario — get current scenario
  if (method === "GET" && path === "/api/scenario") {
    return json(await sendCommand(sim, { cmd: "scenario" }));
  }

  // GET /api/lineage
  if (method === "GET" && path === "/api/lineage") {
    return json(await sendCommand(sim, { cmd: "lineage" }));
  }

  // GET /api/history/:probeId
  if (method === "GET" && path.startsWith("/api/history/")) {
    const probeId = path.slice("/api/history/".length);
    return json(await sendCommand(sim, { cmd: "history", probe_id: probeId }));
  }

  return json({ ok: false, error: "not found" }, 404);
}
