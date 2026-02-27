# Getting Started

## Prerequisites

**Required:**

- GCC with C11 support (`gcc --version` should be 4.9+)
- SQLite3 development headers and shared library (`libsqlite3-dev` on Debian/Ubuntu, `sqlite` on macOS via Homebrew)
- GNU Make

**Optional:**

- Raylib — for the visual (graphical) mode. Pre-built libraries for macOS and Linux are bundled in `vendor/raylib/lib/`.
- Python 3.8+ — for the LLM agent bridge
- An Anthropic API key — for running probes with Claude-powered decision-making

## Building

The project uses a straightforward Makefile with no configure step.

```bash
# Headless simulation binary
make

# Visual mode (requires Raylib)
make visual

# Both
make all visual
```

The build produces `universe` (headless) and/or `universe_visual` (Raylib). Both link against `libsqlite3.so`, so you may need `LD_LIBRARY_PATH=.` if SQLite is bundled locally.

### Platform Notes

The Makefile auto-detects macOS vs Linux via `uname -s` and adjusts linker flags:

- **macOS (Darwin):** Links Raylib with `-framework OpenGL -framework Cocoa -framework IOKit -framework CoreAudio -framework CoreVideo`
- **Linux:** Links with `-lGL -lpthread -ldl -lrt -lX11`

If you're building on a fresh system and get linker errors about OpenGL or X11, install the development packages for your platform.

## Running

### Headless Mode

```bash
LD_LIBRARY_PATH=. ./universe
```

The simulation runs in a loop, persisting state to `universe.db` (SQLite). On first run it seeds the universe and spawns Bob, the original probe. Each tick represents one day of simulation time.

### Visual Mode

```bash
LD_LIBRARY_PATH=. ./universe_visual
```

Opens a Raylib window with three view levels: galaxy view (all explored systems), system view (stars + planets with orbits), and probe view (detailed probe status). Navigation uses keyboard shortcuts and click-to-select.

### Simulation Speed

The default speed target is 1 sim-year per 24 real-minutes at 60 FPS. Speed can be adjusted in visual mode with keyboard controls. The speed system uses a fractional tick accumulator, so sub-frame precision is maintained even at very slow speeds.

## Running Tests

```bash
# All 1,515 tests across 12 phases
make test

# Individual phase
make test1   # Generation (475 tests)
make test2   # Probe actions (170 tests)
make test3   # Travel (55 tests)
make test4   # Agent IPC (113 tests)
make test5   # Render logic (132 tests)
make test6   # Personality (76 tests)
make test7   # Replication (168 tests)
make test8   # Communication (62 tests)
make test9   # Events (60 tests)
make test10  # Society (92 tests)
make test11  # LLM agent (57 tests)
make test12  # Scenario framework (55 tests)
```

Each test binary is self-contained and reports `passed` / `failed` counts. Exit code 0 means all passed.

## Configuration

Runtime parameters can be tuned via the configuration system (Phase 12). The config accepts JSON:

```json
{
  "tick_rate": 60,
  "event_freq_discovery": 0.008,
  "event_freq_hazard": 0.002,
  "mutation_rate": 0.15,
  "repl_base_ticks": 250
}
```

See [Scenario Guide](scenario-guide.md) for details on runtime configuration, event injection, and snapshot/rollback.

## Project Structure

```
UNIVERSE/
  Makefile              Build system
  SPEC.md               Full vision specification
  README.md             Project overview
  universe.db           SQLite database (created at runtime)
  clean_run.sh          Build + test + launch script
  src/                  All C source and headers
  tools/                Test suites
  agents/               External agent implementations
  vendor/               Bundled dependencies (SQLite, Raylib)
  docs/                 Documentation
```

## Next Steps

- Read the [Architecture](architecture.md) doc to understand how the modules fit together
- Read the [Phase Guide](phases.md) for a walkthrough of each implementation phase
- Set up the [LLM Agent](llm-agent.md) to give probes Claude-powered intelligence
- Use the [Scenario Guide](scenario-guide.md) to inject events and fork universes
