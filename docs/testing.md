# Testing

## Philosophy

Project UNIVERSE uses strict test-first development. Every phase follows the same cycle:

1. **Write the header** — define the API contract (function signatures, structs, constants)
2. **Write the test suite** — every behavior has a test before any implementation exists
3. **Create a stub** — empty implementations that compile but do nothing
4. **Verify expected failures** — confirm the stub passes trivial tests (struct init, etc.) and fails the behavioral ones
5. **Implement** — write the real code
6. **Verify all pass** — zero failures across the entire suite

This means the tests define the spec. If behavior isn't tested, it doesn't exist.

## Running Tests

```bash
# All 1,515 tests
make test

# Individual phase
make test1   # through make test12

# Just build without running
make test_generate   # builds the binary
```

Each test binary exits with code 0 (all passed) or 1 (at least one failure). The output format is:

```
=== Phase N: Module Name Tests ===

Test: Description of what's being tested
Test: Another test
  FAIL [tools/test_foo.c:42]: assertion message (got X, expected Y)
Test: More tests...

=== Results: 55 passed, 0 failed ===
```

## Test Structure

Every test file follows the same pattern:

```c
#include <stdio.h>
#include <string.h>
#include <math.h>
#include "../src/module.h"

static int passed = 0, failed = 0;

#define ASSERT(cond, msg) do { \
    if (cond) { passed++; } \
    else { failed++; printf("  FAIL [%s:%d]: %s\n", __FILE__, __LINE__, msg); } \
} while(0)

#define ASSERT_EQ_INT(a, b, msg) do { \
    int _a = (a), _b = (b); \
    if (_a == _b) { passed++; } \
    else { failed++; printf("  FAIL [%s:%d]: %s (got %d, expected %d)\n", \
                            __FILE__, __LINE__, msg, _a, _b); } \
} while(0)

#define ASSERT_NEAR(a, b, eps, msg) do { \
    double _a = (a), _b = (b); \
    if (fabs(_a - _b) < (eps)) { passed++; } \
    else { failed++; printf("  FAIL [%s:%d]: %s (got %.6f, expected %.6f)\n", \
                            __FILE__, __LINE__, msg, _a, _b); } \
} while(0)

static void test_something(void) {
    printf("Test: Something works\n");
    // setup
    // action
    // assert
}

int main(void) {
    printf("=== Phase N: Module Tests ===\n\n");
    test_something();
    // ... more tests ...
    printf("\n=== Results: %d passed, %d failed ===\n", passed, failed);
    return failed > 0 ? 1 : 0;
}
```

No test framework — just three macros (`ASSERT`, `ASSERT_EQ_INT`, `ASSERT_NEAR`) and printf. This keeps tests completely self-contained with zero dependencies beyond the module under test.

## Memory Considerations

`universe_t` is ~90MB and `snapshot_t` is similarly large. These cannot go on the stack. Tests that use universe state must either declare them `static` or allocate on the heap. The Phase 12 tests demonstrate the pattern:

```c
// Good: static allocation (lives in BSS segment)
static universe_t g_uni;
static snapshot_t g_snap;

static void init_universe(universe_t *uni) {
    memset(uni, 0, sizeof(*uni));
    uni->seed = 42;
    // ...
}

static void test_something(void) {
    init_universe(&g_uni);
    // test using &g_uni
}
```

Earlier phases (1-11) use return-by-value helpers like `make_universe()` — these work because those test files were written before `probe_t` grew to its current 88KB size. Phase 12 switched to static globals. If you're adding tests, prefer the static pattern.

## Adding Tests for a New Module

1. Create `tools/test_yourmodule.c` following the pattern above
2. Add to the Makefile:

```makefile
# In the test binary list
TEST13_BIN = test_yourmodule

# Build rule
$(TEST13_BIN): tools/test_yourmodule.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Run target
test13: $(TEST13_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST13_BIN)
	@echo ""

# Update aggregates
test: test1 test2 ... test13
clean: ... $(TEST13_BIN) ...
.PHONY: ... test13
```

3. Add your module's `.c` to `CORE_SRC` if it's a new source file.

## Test Coverage by Phase

| Phase | File | Tests | What's Covered |
|-------|------|-------|----------------|
| 1 | test_generate.c | 475 | Sector generation, star classification, habitable zones, planet types, resources, orbital params, determinism |
| 2 | test_probe.c | 170 | Action validation, state transitions, survey progression, mining, repair, energy ticks, persistence |
| 3 | test_travel.c | 55 | Travel initiation, fuel consumption, arrival detection, sensor scanning, Lorentz factor |
| 4 | test_agent.c | 113 | JSON serialization, action parsing, result encoding, name lookups, fallback agent, framing, routing |
| 5 | test_render.c | 132 | View states, camera transforms, zoom, speed presets, tick accumulation, hit testing, trails, orbital pos |
| 6 | test_personality.c | 76 | Trait drift per event type, memory recording/fading, vivid memory selection, monologue, quirks |
| 7 | test_replicate.c | 168 | Resource checking, multi-tick progress, consciousness fork timing, personality mutation, earth memory degradation, quirk inheritance, naming, lineage tree |
| 8 | test_communicate.c | 62 | Range calculation, light delay, targeted/broadcast send, relay Dijkstra, beacon placement/detection, inbox delivery |
| 9 | test_events.c | 60 | Event generation, hazard damage formulas, alien life probability, civ generation, personality integration, deterministic replay |
| 10 | test_society.c | 92 | Trust updates, trade delivery, territory claims/conflicts, build speed multiplier, voting, tech sharing |
| 11 | test_agent_llm.c | 57 | System prompt building, observation formatting, JSON response parsing, context compression, cost tracking, deliberation throttle, decision logging |
| 12 | test_scenario.c | 55 | Inject queue, JSON injection, targeted injection, metrics recording/computation, snapshot/restore/matching, fork, config set/get/parse, replay step |

## Determinism Testing

Several tests verify that the simulation is deterministic:

- Generation tests confirm same seed produces same systems across runs
- Event tests use `events_deterministic_check()` to verify event type sequences
- Snapshot tests verify that restore + re-snapshot produces an identical snapshot

If you add randomness to any module, make sure it flows through the `rng_t` state (never `rand()` or `random()`), and add a determinism test.
