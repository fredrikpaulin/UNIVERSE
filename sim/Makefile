CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -Ivendor -Isrc
LDFLAGS = -L. -lsqlite3 -lm

# Output directory
BUILD   = build

# Core sources (shared by main and tests)
CORE_SRC = src/rng.c src/arena.c src/persist.c src/generate.c src/probe.c src/travel.c src/agent_ipc.c src/render.c src/personality.c src/replicate.c src/communicate.c src/events.c src/society.c src/agent_llm.c src/scenario.c
CORE_OBJ = $(patsubst src/%.c,$(BUILD)/%.o,$(CORE_SRC))

# Main binary (headless)
BIN = $(BUILD)/universe

# Visual binary (with Raylib) — platform-aware link flags
VISUAL_BIN     = $(BUILD)/universe_visual
RAYLIB_CFLAGS  = -DUSE_RAYLIB -Ivendor/raylib/src
UNAME_S := $(shell uname -s)
RAYLIB_LIB_DIR = vendor/raylib/lib/$(UNAME_S)
ifeq ($(UNAME_S),Darwin)
    RAYLIB_LDFLAGS = -L$(RAYLIB_LIB_DIR) -lraylib -lsqlite3 -framework OpenGL -framework Cocoa -framework IOKit -framework CoreAudio -framework CoreVideo -lm
else
    RAYLIB_LDFLAGS = -L$(RAYLIB_LIB_DIR) -lraylib -L. -lGL -lsqlite3 -lm -lpthread -ldl -lrt -lX11
endif

# Test binaries
TEST1_BIN  = $(BUILD)/test_generate
TEST2_BIN  = $(BUILD)/test_probe
TEST3_BIN  = $(BUILD)/test_travel
TEST4_BIN  = $(BUILD)/test_agent
TEST5_BIN  = $(BUILD)/test_render
TEST6_BIN  = $(BUILD)/test_personality
TEST7_BIN  = $(BUILD)/test_replicate
TEST8_BIN  = $(BUILD)/test_communicate
TEST9_BIN  = $(BUILD)/test_events
TEST10_BIN = $(BUILD)/test_society
TEST11_BIN = $(BUILD)/test_agent_llm
TEST12_BIN = $(BUILD)/test_scenario

all: $(BIN)

$(BIN): $(BUILD)/main.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Visual target
visual: $(CORE_OBJ) $(BUILD)/render_raylib_visual.o $(BUILD)/main_visual.o | $(BUILD)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -o $(VISUAL_BIN) $^ $(RAYLIB_LDFLAGS)

$(BUILD)/main_visual.o: src/main.c | $(BUILD)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c -o $@ $<

$(BUILD)/render_raylib_visual.o: src/render_raylib.c | $(BUILD)
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c -o $@ $<

# Test linking rules
$(TEST1_BIN):  $(BUILD)/test_generate.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
$(TEST2_BIN):  $(BUILD)/test_probe.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
$(TEST3_BIN):  $(BUILD)/test_travel.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
$(TEST4_BIN):  $(BUILD)/test_agent.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
$(TEST5_BIN):  $(BUILD)/test_render.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
$(TEST6_BIN):  $(BUILD)/test_personality.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
$(TEST7_BIN):  $(BUILD)/test_replicate.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
$(TEST8_BIN):  $(BUILD)/test_communicate.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
$(TEST9_BIN):  $(BUILD)/test_events.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
$(TEST10_BIN): $(BUILD)/test_society.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
$(TEST11_BIN): $(BUILD)/test_agent_llm.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)
$(TEST12_BIN): $(BUILD)/test_scenario.o $(CORE_OBJ) | $(BUILD)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Compile rules
$(BUILD)/%.o: src/%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD)/test_%.o: tests/test_%.c | $(BUILD)
	$(CC) $(CFLAGS) -c -o $@ $<

$(BUILD):
	mkdir -p $(BUILD)

clean:
	rm -rf $(BUILD) *.db

# Run all tests
test: test1 test2 test3 test4 test5 test6 test7 test8 test9 test10 test11 test12

test1: $(TEST1_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST1_BIN)
	@echo ""

test2: $(TEST2_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST2_BIN)
	@echo ""

test3: $(TEST3_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST3_BIN)
	@echo ""

test4: $(TEST4_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST4_BIN)
	@echo ""

test5: $(TEST5_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST5_BIN)
	@echo ""

test6: $(TEST6_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST6_BIN)
	@echo ""

test7: $(TEST7_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST7_BIN)
	@echo ""

test8: $(TEST8_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST8_BIN)
	@echo ""

test9: $(TEST9_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST9_BIN)
	@echo ""

test10: $(TEST10_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST10_BIN)
	@echo ""

test11: $(TEST11_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST11_BIN)
	@echo ""

test12: $(TEST12_BIN)
	@echo "==============================="
	LD_LIBRARY_PATH=. ./$(TEST12_BIN)
	@echo ""

.PHONY: all visual clean test test1 test2 test3 test4 test5 test6 test7 test8 test9 test10 test11 test12
