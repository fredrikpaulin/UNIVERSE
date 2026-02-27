# Phase Guide

Project UNIVERSE was built in 12 incremental phases, each adding a self-contained layer of functionality. Every phase follows the same development cycle: write the header (API contract), write the test suite, create a stub implementation, verify expected failures, implement, then verify all tests pass.

## Phase 1: Procedural Universe Generation

**Module:** `generate.h/c` | **Tests:** 475

The foundation. Given a 64-bit seed, deterministically generate a galaxy of star systems. Stars follow the real Hertzsprung-Russell distribution: 76.5% are dim M-class red dwarfs, while blazing O-class blue giants are one in three million. Each star gets a planetary system with orbital parameters, atmospheres, resource distributions, and habitability indices.

Key design: the universe is lazily generated. Only sectors that probes actually visit get materialized. A sector's RNG is derived from `galaxy_seed + (x, y, z)` coordinates, so the same sector always produces the same systems. Once generated, systems are persisted to SQLite and become canonical.

The generation covers: sector star counts (denser near galactic core), spectral classification, habitable zone calculation from stellar luminosity, planet type distribution (gas giants, rocky worlds, ocean worlds, etc.), moon generation, resource abundance per planet type, and the full orbital mechanics (semi-major axis, eccentricity, period).

## Phase 2: Probe State & Actions

**Module:** `probe.h/c` | **Tests:** 170

Probes are the actors in the simulation. Phase 2 establishes the action system: a probe can navigate to a body, enter orbit, land, launch, survey (5 progressive levels of detail), mine resources, wait, or self-repair. Each action is validated against the probe's current state — you can't mine while in orbit, can't land without orbiting first, can't survey without being in the right location.

`probe_init_bob()` creates the original probe with the spec-defined starting configuration. `probe_execute_action()` is the central dispatcher that validates, applies, and returns results. The energy system has a fusion reactor that converts hydrogen fuel into joules each tick.

## Phase 3: Interstellar Travel

**Module:** `travel.h/c` | **Tests:** 55

Moving between star systems. `travel_initiate()` sets the heading, computes estimated arrival time, and transitions the probe to `STATUS_TRAVELING`. `travel_tick()` advances position, consumes fuel proportional to speed, and checks for arrival. If fuel runs out mid-transit, the probe is stranded.

Long-range sensors (`travel_scan()`) let probes detect nearby systems within their sensor range, sorted by distance. The Lorentz factor calculation is included for relativistic time dilation — probes traveling at significant fractions of c experience time differently.

## Phase 4: Agent IPC Protocol

**Module:** `agent_ipc.h/c` | **Tests:** 113

The bridge between the C simulation and external decision-makers. The protocol is newline-delimited JSON over Unix domain sockets. Each tick, the simulation serializes a probe's state and surroundings into an observation JSON blob, sends it to the connected agent, and receives an action JSON blob back.

`obs_serialize()` packs everything an agent needs to decide: probe status, position, resources, tech levels, nearby planets, and recent events. `action_parse()` decodes the response. The `agent_router_t` maps probe IDs to socket file descriptors. A `fallback_agent_decide()` provides simple survive-logic (repair if damaged, wait otherwise) for probes without a connected agent.

Includes name-to-enum lookups for resources and action types, making the JSON human-readable.

## Phase 5: Visualization & Rendering

**Module:** `render.h/c` | **Tests:** 132

The rendering logic layer. This is deliberately separated from Raylib — all the logic (view states, camera math, speed control) is in `render.h/c` and fully testable without a GPU. The actual draw calls live in `render_raylib.c`, compiled only with `-DUSE_RAYLIB`.

Three view levels: galaxy (all explored systems as dots), system (stars + planets with animated orbits), and probe (detailed status panel). The 2D camera supports zoom and pan with `world_to_screen()` / `screen_to_world()` coordinate transforms. Hit testing identifies which system you clicked on.

The speed system uses a fractional tick accumulator. Default target: 1 sim-year per 24 real-minutes at 60 FPS, which works out to ~0.25 ticks per frame. Speed can be ramped up or down through preset levels. The accumulator handles sub-frame precision so ticks don't stutter at slow speeds.

Probe trails are stored in ring buffers for path visualization.

## Phase 6: Personality & Memory

**Module:** `personality.h/c` | **Tests:** 76

The beginning of probe individuality. Each probe has 10 personality traits: curiosity, caution, sociability, humor, empathy, ambition, creativity, stubbornness, existential angst, and nostalgia for earth. All traits live on a [-1, 1] scale.

Personality drifts in response to events. Finding a beautiful high-habitability system increases curiosity and decreases existential angst. Taking hull damage increases caution. Long stretches of solitude increase angst and decrease sociability. The drift rate itself is a trait — some probes are more psychologically stable than others.

The memory system records episodic memories with emotional weight. Memories fade over time: each tick, fading increases slightly. The most vivid memories (recent or emotionally charged) persist longest. When memory slots are full, the most faded memory is evicted.

`monologue_generate()` produces inner thoughts based on personality + recent event. Quirks give probes idiosyncratic behaviors — naming systems after foods when stressed, for example.

## Phase 7: Self-Replication

**Module:** `replicate.h/c` | **Tests:** 168

Von Neumann self-replication. A probe needs 500,000 kg of resources across 9 types (iron, silicon, rare earth, carbon, water, uranium, hydrogen, helium-3, exotic matter). Replication takes 200 base ticks with a consciousness fork at 80% completion.

The child inherits and mutates the parent's personality via gaussian noise. Earth memories degrade per generation — fidelity drops and strings get truncated. By generation 3-4, earth memories are fragments. Quirks follow a 70% keep / 10% mutate / 20% drop rule, with a chance of gaining a new random quirk.

`name_generate_child()` creates variant names from the parent. The lineage tree tracks the full parent→child family tree with birth ticks and generation numbers.

## Phase 8: Communication

**Module:** `communicate.h/c` | **Tests:** 62

Light-speed constrained messaging. Messages travel at 1 ly/year, which at 1 tick = 1 day means 365 ticks per light-year of distance. A message to a probe 10 light-years away takes 3,650 ticks to arrive.

Two modes: targeted (point-to-point) and broadcast (all probes in direct range). Both cost energy. Communication range is `5 + 5 * comm_tech_level` light-years.

Relay satellites extend range. `comm_relay_path_distance()` uses Dijkstra's algorithm to find the shortest path through a chain of relays — source reaches relays within its direct range, relays reach other relays within their 20 ly relay range, creating multi-hop paths.

Beacons are passive markers placed in star systems. When a probe enters a system, it can detect beacons and read their messages — a breadcrumb system for exploration coordination.

## Phase 9: Events & Encounters

**Module:** `events.h/c` | **Tests:** 60

The stochastic event engine. Each tick a probe is in a system, it rolls against per-type probabilities: discovery 0.5%, hazard 0.2%, anomaly 0.1%, encounter 0.02%, crisis 0.005%, wonder 0.03%.

Hazards have teeth. Solar flares damage hull (mitigated by materials tech), asteroids hit hull directly, radiation bursts damage compute capacity. Crises combine hull damage with resource loss.

The alien life system is a probability chain. Base chance on a habitable planet is ~0.01%, weighted by habitability and water coverage. Life type follows a distribution: 40% microbial, 20% multicellular, down to 0.5% transcended. Extinct civilizations (7% of life-bearing planets) always leave 2-5 artifacts. Living civilizations get procedural names (16 prefixes × 16 suffixes), biology bases, disposition, and cultural traits.

Events integrate with personality: discoveries trigger curiosity drift, hazards trigger caution drift, and all significant events are recorded as memories.

## Phase 10: Society

**Module:** `society.h/c` | **Tests:** 92

Emergent probe civilization. As probes replicate and spread, they form relationships through interaction.

**Trust** is the core social mechanic. Trust between two probes changes based on actions: completing a trade (+0.05), sharing tech (+0.08), collaborating on construction (+0.06), violating a territory claim (-0.10), disagreeing in a vote (-0.05).

**Resource trading** moves materials between probes — instant within the same system, 100-tick transit delay across systems. **Territory claims** create a property system. Entering a claimed system without permission triggers a trust penalty.

**Shared construction** builds structures (mining stations, relay satellites, observatories, habitats, shipyards, factories). Multiple builders speed up construction: `1 + 0.6 * (builders - 1)`, up to 4 collaborators. Each structure type has iron/silicon costs and base construction times.

**Voting** resolves group decisions. Any probe can propose, all can vote. Proposals have deadlines and resolve by majority. Duplicate votes are rejected.

**Tech sharing** lets an advanced probe teach a domain to a less advanced one. The recipient jumps to the sender's level at 40% of normal research cost.

## Phase 11: The LLM Awakens

**Module:** `agent_llm.h/c` + `agents/agent_llm.py` | **Tests:** 57

Connecting probes to large language models. The C side builds prompts and parses responses; the Python side handles the API call.

`llm_build_system_prompt()` constructs a rich personality prompt: probe name, trait descriptions ("deeply curious but cautious"), quirks, earth memories (for earlier generations), current goals, and the expected response format. `llm_build_observation()` formats the game state: tick, hull/energy/fuel status, location, tech levels, recent events.

`llm_parse_response()` uses a hand-rolled JSON parser (no external libraries) to extract an action array and monologue text from the LLM response. Action type strings ("survey", "mine", "navigate") map to the existing `action_t` enum.

Context management uses a rolling summary. Events accumulate as semicolon-separated text. When the count exceeds the compression interval, the summary is trimmed to its latter half at a clean boundary — keeping recent context, discarding old.

Cost tracking monitors input/output tokens and USD spend per call. Deliberation throttling ensures the LLM is only invoked every N ticks (default 10) to manage costs, with a `force_next` flag for significant events.

## Phase 12: Scenario & Polish

**Module:** `scenario.h/c` | **Tests:** 55

Tools for experimentation and analysis.

**Event injection** lets you queue scripted events (by type, subtype, severity, and optional probe target) that fire on the next tick. Supports JSON parsing for external tooling. Targeted injection hits only a specific probe; untargeted hits all probes.

**Metrics** samples simulation health at configurable intervals: systems explored, probes spawned, average tech level, average trust, total discoveries, hazards survived, civilizations found.

**Snapshot/rollback** captures the full universe state (tick, seed, all probes) and restores it. `snapshot_matches()` verifies two snapshots are identical by doing probe-by-probe memcmp.

**Universe forking** clones a snapshot with a new RNG seed. Same starting state, different future — perfect for "what if" experiments.

**Configuration** is a JSON-parseable key-value store for runtime parameter tuning. Any simulation constant can be overridden without recompiling.

**Replay** extracts events from a tick range and steps through them one tick at a time for historical playback.
