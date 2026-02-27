# LLM Agent Guide

## Overview

Project UNIVERSE can connect probes to large language models for decision-making. Instead of the simple fallback agent (repair if damaged, wait otherwise), a probe connected to an LLM gets rich contextual prompts about its situation and returns nuanced decisions with inner monologue.

The architecture is split: the C simulation builds prompts and parses responses (`agent_llm.c`), while a Python bridge (`agents/agent_llm.py`) handles the actual API call to Anthropic's Claude.

## Setup

### 1. Python Environment

```bash
# Install the Anthropic SDK (optional — the agent falls back to urllib)
pip install anthropic

# Or use without any dependencies (urllib fallback)
python3 agents/agent_llm.py
```

### 2. API Key

Set your Anthropic API key as an environment variable:

```bash
export ANTHROPIC_API_KEY="sk-ant-..."
```

### 3. Running

Start the simulation first, then connect the Python agent:

```bash
# Terminal 1: Start simulation
LD_LIBRARY_PATH=. ./universe

# Terminal 2: Connect LLM agent
python3 agents/agent_llm.py
```

The Python agent connects via Unix domain socket, receives JSON observations each tick, calls the Anthropic API, and returns JSON actions.

## How It Works

### Prompt Construction

Each deliberation cycle, the C side builds a multi-part prompt:

**System prompt** (`llm_build_system_prompt`) — establishes the probe's identity:
- Probe name and generation
- Personality description (e.g., "You are deeply curious but cautious. You have a dry sense of humor.")
- Active quirks
- Earth memories (if any remain from parent generations)
- Current goals
- Expected JSON response format

**Observation** (`llm_build_observation`) — current game state:
- Current tick
- Hull integrity, energy, fuel levels
- Location (system name, nearby planets with survey status, or "deep space" if traveling)
- Tech levels across all 10 domains
- Recent events since last deliberation

**Memory context** (`llm_build_memory_context`) — psychological state:
- Top N most vivid episodic memories
- Rolling summary of past events

**Relationship context** (`llm_build_relationship_context`) — social awareness:
- Known probes with trust levels and disposition

### Response Format

The LLM responds with JSON:

```json
{
  "actions": [
    {"type": "survey", "target_body": "planet_3", "survey_level": 2}
  ],
  "monologue": "This system has potential. The third planet's habitability readings are promising...",
  "reasoning": "High habitability + unmined resources = good candidate for extended survey"
}
```

`llm_parse_response()` extracts the actions (mapped to `action_t` enums) and the monologue. The hand-rolled JSON parser handles nested objects and arrays without any external library.

### Supported Action Types

The LLM can request any standard probe action by name:

| Action String | Enum | Description |
|--------------|------|-------------|
| `"survey"` | `ACT_SURVEY` | Survey a body (specify level 0-4) |
| `"mine"` | `ACT_MINE` | Extract resources from landed body |
| `"navigate"` | `ACT_NAVIGATE_TO_BODY` | Move to a body in current system |
| `"orbit"` | `ACT_ENTER_ORBIT` | Enter orbit around a body |
| `"land"` | `ACT_LAND` | Land on a body surface |
| `"launch"` | `ACT_LAUNCH` | Launch from surface to orbit |
| `"repair"` | `ACT_REPAIR` | Self-repair hull |
| `"wait"` | `ACT_WAIT` | Do nothing this tick |

## Deliberation Throttling

Calling an LLM every tick is expensive and usually unnecessary. The deliberation system controls call frequency:

- **Default interval:** Every 10 ticks (configurable)
- **Force deliberation:** After significant events (hazard, discovery, arrival at new system), `llm_delib_force()` ensures the next tick triggers a full LLM call
- **Between deliberations:** The probe repeats its last action or uses the fallback agent

```c
// Check if this tick needs LLM input
if (llm_delib_should_call(&delib, current_tick)) {
    // Build prompt, call LLM, parse response
    llm_delib_record(&delib, current_tick);
}
```

## Cost Tracking

The cost tracker monitors API usage:

```c
llm_cost_tracker_t tracker;
llm_cost_init(&tracker, 0.000003, 0.000015);  // input/output rates per token

// After each call
llm_cost_record(&tracker, input_tokens, output_tokens);

// Check spend
double avg = llm_cost_avg_per_call(&tracker);
double total = tracker.total_cost_usd;
```

Default rates are set for Claude Haiku-class pricing. Adjust `cost_per_token_input` and `cost_per_token_output` for your model tier.

## Context Management

To keep prompts within token limits, the context manager maintains a rolling summary:

```c
llm_context_t ctx;
llm_context_init(&ctx, 20);  // compress every 20 events

// Each tick with events
llm_context_append_event(&ctx, "Discovered mineral deposit on planet 3");
llm_context_append_event(&ctx, "Solar flare caused 0.15 hull damage");

// Auto-compresses: keeps the latter half of accumulated events
const char *summary = llm_context_get_summary(&ctx);
```

Events accumulate as semicolon-separated text. When the count exceeds the compression interval, the summary is trimmed at a clean semicolon boundary, keeping recent events and discarding old ones.

## Decision Logging

Every LLM decision is logged for analysis:

```c
llm_decision_log_t log;
llm_log_init(&log);

llm_log_record(&log, tick, probe_id, &action, monologue,
               input_tokens, output_tokens);

// Query later
llm_decision_log_entry_t entries[100];
int n = llm_log_get_for_probe(&log, probe_id, entries, 100);
```

Each log entry captures: tick, probe ID, observation hash, chosen action, monologue text, and token counts.

## Personality Integration

The personality flavor text generator translates numeric traits into natural language for the system prompt:

- Curiosity 0.8 → "deeply curious"
- Caution -0.5 → "bold and reckless"
- Humor 0.6 → "with a dry sense of humor"
- Existential angst 0.9 → "often contemplating the nature of existence"

This gives the LLM a character to inhabit rather than raw numbers. Combined with quirks and earth memories, each probe's prompts feel distinct even when facing similar situations.

## Tips for Best Results

**Use longer deliberation intervals for routine situations.** A probe surveying a system doesn't need LLM input every tick. Set the interval to 20-50 for exploration, and use `force_next` for critical moments.

**Monitor token costs.** With 1,024 potential probes, LLM costs can add up. Consider running LLM agents only for "interesting" probes (first generation, probes encountering civilizations, etc.) and using the fallback agent for the rest.

**The monologue is the payoff.** The most engaging part of LLM integration is reading what probes think. The monologue field captures inner thoughts that reflect personality drift, earth nostalgia, and relationships — this is what makes checking in on the simulation feel like reading a story.
