#!/usr/bin/env python3
"""
agent.py — LLM agent for Project UNIVERSE

Connects to the universe server via WebSocket, receives observations,
constructs a prompt with probe personality and context, sends to the
Anthropic API, and returns actions.

Usage:
    python3 agents/llm/agent.py --api-key $ANTHROPIC_API_KEY
                                [--url ws://localhost:8000/ws]
                                [--probe 1-1]
                                [--model claude-sonnet-4-20250514]
                                [--deliberation-interval 10]

Requires: pip install websockets
"""

import asyncio
import json
import sys
import os
import argparse
import urllib.request
import urllib.error


def build_system_prompt(obs):
    """Build system prompt from first observation fields."""
    name = obs.get("name", "Probe")

    prompt = (
        f"You are {name}, a Von Neumann probe — a self-replicating spacecraft "
        f"carrying a digitized human consciousness.\n\n"
        f"You exist in a procedurally generated universe. Each tick you receive "
        f"an observation of your state and surroundings, and must choose one action.\n\n"
    )

    prompt += (
        "Respond with JSON only:\n"
        '{"action":"<action_name>", ...optional fields..., '
        '"monologue":"<your inner thoughts>", '
        '"reasoning":"<why this action>"}\n\n'
        "Available actions:\n"
        "  wait           — do nothing\n"
        "  survey         — scan current body (progressive detail levels 0-4)\n"
        "  mine           — mine a resource (add \"resource\":\"iron\" etc)\n"
        "  repair         — self-repair hull\n"
        "  navigate_to_body — move to body (add \"target_body_hi\":N, \"target_body_lo\":N)\n"
        "  enter_orbit    — enter orbit around current body\n"
        "  land           — land on surface\n"
        "  launch         — launch from surface to orbit\n\n"
        "Resources: iron, silicon, rare_earth, water, hydrogen, helium3, carbon, uranium, exotic\n"
    )
    return prompt


def call_llm(system_prompt, observation_text, api_key, model):
    """Call Anthropic API. Returns (response_text, input_tokens, output_tokens)."""
    body = json.dumps({
        "model": model,
        "max_tokens": 512,
        "system": system_prompt,
        "messages": [{"role": "user", "content": observation_text}],
    }).encode()

    req = urllib.request.Request(
        "https://api.anthropic.com/v1/messages",
        data=body,
        headers={
            "Content-Type": "application/json",
            "x-api-key": api_key,
            "anthropic-version": "2023-06-01",
        },
    )

    try:
        with urllib.request.urlopen(req, timeout=30) as resp:
            data = json.loads(resp.read())
            text = data["content"][0]["text"]
            usage = data.get("usage", {})
            return text, usage.get("input_tokens", 0), usage.get("output_tokens", 0)
    except urllib.error.HTTPError as e:
        print(f"API error: {e.code} {e.read().decode()}", file=sys.stderr)
        return None, 0, 0
    except Exception as e:
        print(f"API call failed: {e}", file=sys.stderr)
        return None, 0, 0


def parse_llm_action(text):
    """Extract action dict from LLM response. Returns (action, monologue)."""
    try:
        # Strip markdown code fences if present
        clean = text.strip()
        if clean.startswith("```"):
            clean = clean.split("\n", 1)[1] if "\n" in clean else clean[3:]
            if clean.endswith("```"):
                clean = clean[:-3]
            clean = clean.strip()

        data = json.loads(clean)
        monologue = data.pop("monologue", "")
        data.pop("reasoning", None)

        # Ensure we have an action field
        if "action" not in data:
            # Legacy format: {"actions":[{"type":"survey"}]}
            actions = data.get("actions", [])
            if actions and "type" in actions[0]:
                action = {"action": actions[0]["type"]}
                for k, v in actions[0].items():
                    if k != "type":
                        action[k] = v
                return action, monologue
            return {"action": "wait"}, monologue

        return data, monologue
    except (json.JSONDecodeError, KeyError, IndexError):
        return {"action": "wait"}, ""


def discover_probe(base_url):
    """Fetch /api/status and return first probe ID, or None."""
    try:
        url = base_url.replace("ws://", "http://").replace("wss://", "https://").replace("/ws", "/api/status")
        with urllib.request.urlopen(url, timeout=5) as resp:
            data = json.loads(resp.read())
            probes = data.get("probes", [])
            if probes:
                return probes[0]["id"]
    except Exception as e:
        print(f"Auto-discover failed: {e}", file=sys.stderr)
    return None


async def run_agent(args):
    """Main agent loop."""
    import websockets

    probe_id = args.probe
    if not probe_id:
        probe_id = discover_probe(args.url)
        if not probe_id:
            print("No probes found. Start the server first.", file=sys.stderr)
            sys.exit(1)
        print(f"Auto-discovered probe {probe_id}")

    print(f"Connecting to {args.url} ...")
    async with websockets.connect(args.url) as ws:
        # Register
        await ws.send(json.dumps({"type": "register", "probe_id": probe_id}))

        # Wait for registration confirmation
        reg = json.loads(await ws.recv())
        if reg.get("type") == "registered":
            print(f"Registered for probe {reg['probe_id']}")
        else:
            print(f"Unexpected: {reg}", file=sys.stderr)

        system_prompt = None
        tick_count = 0
        total_in = 0
        total_out = 0

        print(f"Deliberation every {args.deliberation_interval} ticks. Ctrl+C to stop.\n")

        try:
            async for raw in ws:
                msg = json.loads(raw)

                if msg.get("type") != "observe":
                    continue

                tick_count += 1

                # Build system prompt on first observation
                if system_prompt is None:
                    system_prompt = build_system_prompt(msg)

                # Deliberation throttle: only call LLM every N ticks
                if tick_count > 1 and tick_count % args.deliberation_interval != 1:
                    await ws.send(json.dumps({"action": "wait"}))
                    continue

                # Call LLM
                obs_text = json.dumps({k: v for k, v in msg.items() if k != "type"}, indent=2)
                resp_text, in_tok, out_tok = call_llm(
                    system_prompt, obs_text, args.api_key, args.model
                )
                total_in += in_tok
                total_out += out_tok

                if resp_text:
                    action, monologue = parse_llm_action(resp_text)
                    if monologue:
                        print(f"[tick {msg.get('tick', '?')}] {monologue}")
                    await ws.send(json.dumps(action))
                else:
                    await ws.send(json.dumps({"action": "wait"}))

        except websockets.ConnectionClosed:
            print("Server closed connection.")
        except KeyboardInterrupt:
            pass

        print(f"\nTotal: {tick_count} ticks, {total_in} input tokens, {total_out} output tokens")


def main():
    parser = argparse.ArgumentParser(description="LLM agent for Project UNIVERSE")
    parser.add_argument("--url", default="ws://localhost:8000/ws", help="Server WebSocket URL")
    parser.add_argument("--probe", default=None, help="Probe ID (e.g. 1-1). Auto-discovers if omitted")
    parser.add_argument("--api-key", default=os.environ.get("ANTHROPIC_API_KEY"))
    parser.add_argument("--model", default="claude-sonnet-4-20250514")
    parser.add_argument("--deliberation-interval", type=int, default=10,
                        help="Call LLM every N ticks (default 10)")
    args = parser.parse_args()

    if not args.api_key:
        print("Error: --api-key or ANTHROPIC_API_KEY required", file=sys.stderr)
        sys.exit(1)

    asyncio.run(run_agent(args))


if __name__ == "__main__":
    main()
