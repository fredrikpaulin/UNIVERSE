# LLM Agent Guide

## Overview

Project UNIVERSE can connect probes to large language models for decision-making. Instead of the simple fallback agent (repair if damaged, wait otherwise), a probe connected to an LLM gets contextual prompts about its situation and returns nuanced decisions with inner monologue.

The agent (`agents/llm/agent.py`) connects to the Bun server via WebSocket, receives observations each tick, calls the Anthropic Claude API, parses the response into a game action, and sends it back. The C simulation handles prompt-building helpers and response parsing on the backend (`agent_llm.c`), but the Python agent is self-contained for the WebSocket flow.

## Setup

### 1. Python Environment

```bash
# Only dependency: websockets
pip install websockets

# Verify
python3 -c "import websockets; print('ok')"
```

The agent uses `urllib` from stdlib for the Anthropic API call — no SDK needed.

### 2. API Key

```bash
export ANTHROPIC_API_KEY="sk-ant-..."
```

### 3. Running

Start the server, then connect the agent:

```bash
# Terminal 1: Start the universe server
cd server && npx bun run src/index.js

# Terminal 2: Connect LLM agent (auto-discovers first probe)
python3 agents/llm/agent.py --api-key $ANTHROPIC_API_KEY

# Or specify a probe and server URL
python3 agents/llm/agent.py --probe 1-1 --url ws://localhost:8000/ws
```

### CLI Options

| Flag | Default | Description |
|------|---------|-------------|
| `--url` | `ws://localhost:8000/ws` | Server WebSocket URL |
| `--probe` | auto-discover | Probe ID (`"1-1"` format) |
| `--api-key` | `$ANTHROPIC_API_KEY` | Anthropic API key |
| `--model` | `claude-sonnet-4-20250514` | Model to use |
| `--deliberation-interval` | `10` | Call LLM every N ticks |

## How It Works

### Connection Flow

1. Agent fetches `GET /api/status` to discover probe IDs (unless `--probe` is given)
2. Opens WebSocket to `/ws`
3. Sends `{"type":"register","probe_id":"1-1"}`
4. Receives `{"type":"registered","probe_id":"1-1"}`
5. Enters tick loop: receive observation → decide → send action

### Prompt Construction

On the first observation, the agent builds a system prompt from the probe's state (name, personality traits, quirks, earth memories). This prompt is reused for all subsequent LLM calls.

The observation is sent as the user message, formatted as indented JSON with fields like hull, energy, fuel, location, status, tech levels.

### Response Format

The LLM responds with JSON:

```json
{
  "action": "survey",
  "monologue": "This system has potential. The third planet's readings are promising...",
  "reasoning": "High habitability + unmined resources = good candidate"
}
```

The agent strips `monologue` and `reasoning`, then sends the action object to the server. It also handles the legacy format (`{"actions":[{"type":"survey"}]}`) for backwards compatibility.

### Available Actions

| Action | Extra fields | Description |
|--------|-------------|-------------|
| `wait` | — | Do nothing |
| `survey` | — | Scan current body (levels 0-4) |
| `mine` | `resource` | Mine: iron, silicon, rare_earth, water, hydrogen, helium3, carbon, uranium, exotic |
| `repair` | — | Self-repair hull |
| `navigate_to_body` | `target_body_hi`, `target_body_lo` | Move to a body in current system |
| `enter_orbit` | — | Enter orbit around current body |
| `land` | — | Land on body surface |
| `launch` | — | Launch from surface to orbit |

## Deliberation Throttling

Calling an LLM every tick is expensive and usually unnecessary. The `--deliberation-interval` flag controls call frequency:

- **Default:** Every 10 ticks
- **Between deliberations:** The agent sends `{"action":"wait"}` immediately (no API call)
- **Tip:** Use 20-50 for routine exploration, lower for critical situations

## C-Side Helpers

The C simulation provides additional prompt-building and analysis functions in `agent_llm.c`:

- `llm_build_system_prompt()` — richer prompt with personality flavor text
- `llm_build_observation()` — detailed game state including nearby planets
- `llm_build_memory_context()` — vivid memories + rolling summary
- `llm_build_relationship_context()` — known probes with trust levels
- `llm_parse_response()` — hand-rolled JSON parser
- `llm_cost_*()` — token cost tracking
- `llm_delib_*()` — deliberation scheduling with force-on-event
- `llm_log_*()` — decision audit trail

These are available when the simulation drives the prompts directly. The Python agent currently builds its own simpler prompts from the observation data. A future enhancement could have the server forward the C-built prompts to agents.

## Personality Integration

The system prompt translates observation fields into character traits:

- Curiosity > 0.5 → "deeply curious"
- Caution > 0.5 → "cautious", < -0.3 → "bold"
- Humor > 0.5 → "witty"
- Empathy > 0.5 → "empathetic"

Combined with quirks and earth memories from the observation, each probe's prompts feel distinct even in similar situations.

## Tips

**Use longer deliberation intervals for routine situations.** A probe surveying a system doesn't need LLM input every tick. Set the interval to 20-50 for exploration.

**Monitor token costs.** With many probes, LLM costs add up. Consider running LLM agents only for "interesting" probes and using the fallback (wait) for the rest.

**The monologue is the payoff.** The most engaging part is reading what probes think. The monologue captures inner thoughts reflecting personality, nostalgia, and relationships — this is what makes checking in feel like reading a story.
