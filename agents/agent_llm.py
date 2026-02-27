#!/usr/bin/env python3
"""
agent_llm.py — LLM agent for Project UNIVERSE

Connects to the simulation via Unix socket, receives observations as
newline-delimited JSON, constructs a prompt with probe personality and
context, sends to an LLM API, and returns actions.

Usage:
    python3 agents/agent_llm.py --socket /tmp/universe.sock --probe-id 1
                                --api-key $ANTHROPIC_API_KEY
                                [--model claude-sonnet-4-20250514]
                                [--deliberation-interval 10]
"""

import json
import socket
import sys
import os
import argparse
import time


def build_system_prompt(probe_state):
    """Build a system prompt from probe state."""
    name = probe_state.get("name", "Probe")
    personality = probe_state.get("personality", {})
    quirks = probe_state.get("quirks", [])
    earth_memories = probe_state.get("earth_memories", [])

    prompt = (
        f"You are {name}, a Von Neumann probe — a self-replicating spacecraft "
        f"carrying a digitized human consciousness.\n\n"
    )

    # Personality traits
    traits = []
    if personality.get("curiosity", 0) > 0.5:
        traits.append("deeply curious")
    if personality.get("caution", 0) > 0.5:
        traits.append("cautious")
    elif personality.get("caution", 0) < -0.3:
        traits.append("bold")
    if personality.get("humor", 0) > 0.5:
        traits.append("witty")
    if personality.get("empathy", 0) > 0.5:
        traits.append("empathetic")
    if traits:
        prompt += f"Personality: {', '.join(traits)}.\n"

    if quirks:
        prompt += "\nQuirks:\n" + "\n".join(f"- {q}" for q in quirks) + "\n"

    if earth_memories:
        prompt += "\nEarth memories:\n" + "\n".join(f"- {m}" for m in earth_memories) + "\n"

    prompt += (
        "\nRespond with JSON:\n"
        '{"actions":[{"type":"<action>", ...}], '
        '"monologue":"<your inner voice>", '
        '"reasoning":"<why>"}\n'
        "Actions: survey, mine, navigate_to_body, enter_orbit, land, launch, wait, repair\n"
    )
    return prompt


def call_llm(system_prompt, observation, api_key, model="claude-sonnet-4-20250514"):
    """Call the Anthropic API. Returns response text and token counts."""
    try:
        import httpx
    except ImportError:
        # Fallback to urllib
        import urllib.request
        import urllib.error

        body = json.dumps({
            "model": model,
            "max_tokens": 1024,
            "system": system_prompt,
            "messages": [{"role": "user", "content": observation}],
        })

        req = urllib.request.Request(
            "https://api.anthropic.com/v1/messages",
            data=body.encode(),
            headers={
                "Content-Type": "application/json",
                "x-api-key": api_key,
                "anthropic-version": "2023-06-01",
            },
        )

        try:
            with urllib.request.urlopen(req) as resp:
                data = json.loads(resp.read())
                text = data["content"][0]["text"]
                usage = data.get("usage", {})
                return text, usage.get("input_tokens", 0), usage.get("output_tokens", 0)
        except urllib.error.HTTPError as e:
            print(f"API error: {e.code} {e.read().decode()}", file=sys.stderr)
            return None, 0, 0

    # httpx path
    resp = httpx.post(
        "https://api.anthropic.com/v1/messages",
        json={
            "model": model,
            "max_tokens": 1024,
            "system": system_prompt,
            "messages": [{"role": "user", "content": observation}],
        },
        headers={
            "x-api-key": api_key,
            "anthropic-version": "2023-06-01",
        },
        timeout=30.0,
    )
    data = resp.json()
    text = data["content"][0]["text"]
    usage = data.get("usage", {})
    return text, usage.get("input_tokens", 0), usage.get("output_tokens", 0)


def connect_to_sim(socket_path):
    """Connect to the simulation Unix socket."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(socket_path)
    return sock


def recv_line(sock):
    """Receive a newline-delimited JSON message."""
    buf = b""
    while True:
        chunk = sock.recv(4096)
        if not chunk:
            return None
        buf += chunk
        if b"\n" in buf:
            line, _ = buf.split(b"\n", 1)
            return json.loads(line.decode())


def send_line(sock, data):
    """Send a newline-delimited JSON message."""
    msg = json.dumps(data) + "\n"
    sock.sendall(msg.encode())


def main():
    parser = argparse.ArgumentParser(description="LLM agent for Project UNIVERSE")
    parser.add_argument("--socket", default="/tmp/universe.sock")
    parser.add_argument("--probe-id", type=int, required=True)
    parser.add_argument("--api-key", default=os.environ.get("ANTHROPIC_API_KEY"))
    parser.add_argument("--model", default="claude-sonnet-4-20250514")
    parser.add_argument("--deliberation-interval", type=int, default=10)
    args = parser.parse_args()

    if not args.api_key:
        print("Error: --api-key or ANTHROPIC_API_KEY required", file=sys.stderr)
        sys.exit(1)

    print(f"Connecting to {args.socket}...")
    sock = connect_to_sim(args.socket)

    # Register probe
    send_line(sock, {"type": "register", "probe_id": args.probe_id})

    system_prompt = None
    tick_count = 0
    total_input_tokens = 0
    total_output_tokens = 0

    print(f"Agent ready. Deliberation every {args.deliberation_interval} ticks.")

    while True:
        obs = recv_line(sock)
        if obs is None:
            print("Connection closed.")
            break

        # Build system prompt on first observation
        if system_prompt is None and "self" in obs:
            system_prompt = build_system_prompt(obs["self"])

        tick_count += 1

        # Deliberation throttle
        if tick_count % args.deliberation_interval != 1 and tick_count > 1:
            send_line(sock, {"actions": [{"type": "wait"}]})
            continue

        # Build observation text
        obs_text = json.dumps(obs, indent=2)

        # Call LLM
        response_text, in_tok, out_tok = call_llm(
            system_prompt or "You are a space probe.", obs_text,
            args.api_key, args.model
        )

        total_input_tokens += in_tok
        total_output_tokens += out_tok

        if response_text:
            try:
                response = json.loads(response_text)
                monologue = response.get("monologue", "")
                if monologue:
                    print(f"[Tick {obs.get('tick', '?')}] {monologue}")
                send_line(sock, response)
            except json.JSONDecodeError:
                print(f"Bad LLM response: {response_text[:100]}...", file=sys.stderr)
                send_line(sock, {"actions": [{"type": "wait"}]})
        else:
            send_line(sock, {"actions": [{"type": "wait"}]})

    print(f"Total: {tick_count} ticks, {total_input_tokens} input tokens, "
          f"{total_output_tokens} output tokens")
    sock.close()


if __name__ == "__main__":
    main()
