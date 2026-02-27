CC      = gcc
CFLAGS  = -std=c11 -Wall -Wextra -O2 -Ivendor -Isrc
LDFLAGS = -L. -lsqlite3 -lm

# Core sources (shared by main and tests)
CORE_SRC = src/rng.c src/arena.c src/persist.c src/generate.c src/probe.c src/travel.c src/agent_ipc.c src/render.c src/personality.c src/replicate.c src/communicate.c src/events.c src/society.c src/agent_llm.c src/scenario.c
CORE_OBJ = $(CORE_SRC:.c=.o)

# Main binary (headless)
SRC     = src/main.c $(CORE_SRC)
OBJ     = $(SRC:.c=.o)
BIN     = universe

# Visual binary (with Raylib) — platform-aware link flags
VISUAL_BIN     = universe_visual
RAYLIB_CFLAGS  = -DUSE_RAYLIB -Ivendor/raylib/src
UNAME_S := $(shell uname -s)
RAYLIB_LIB_DIR = vendor/raylib/lib/$(UNAME_S)
ifeq ($(UNAME_S),Darwin)
    RAYLIB_LDFLAGS = -L$(RAYLIB_LIB_DIR) -lraylib -lsqlite3 -framework OpenGL -framework Cocoa -framework IOKit -framework CoreAudio -framework CoreVideo -lm
else
    RAYLIB_LDFLAGS = -L$(RAYLIB_LIB_DIR) -lraylib -L. -lGL -lsqlite3 -lm -lpthread -ldl -lrt -lX11
endif

# Test binaries
TEST1_BIN = test_generate
TEST2_BIN = test_probe
TEST3_BIN = test_travel
TEST4_BIN = test_agent
TEST5_BIN = test_render
TEST6_BIN = test_personality
TEST7_BIN = test_replicate
TEST8_BIN = test_communicate
TEST9_BIN = test_events
TEST10_BIN = test_society
TEST11_BIN = test_agent_llm
TEST12_BIN = test_scenario

all: $(BIN)

$(BIN): $(OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

# Visual target — compile main.c and render_raylib.c with Raylib flags
visual: $(CORE_OBJ) src/render_raylib_visual.o src/main_visual.o
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -o $(VISUAL_BIN) $^ $(RAYLIB_LDFLAGS)

src/main_visual.o: src/main.c
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c -o $@ $<

src/render_raylib_visual.o: src/render_raylib.c
	$(CC) $(CFLAGS) $(RAYLIB_CFLAGS) -c -o $@ $<

$(TEST1_BIN): tools/test_generate.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST2_BIN): tools/test_probe.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST3_BIN): tools/test_travel.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST4_BIN): tools/test_agent.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST5_BIN): tools/test_render.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST6_BIN): tools/test_personality.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST7_BIN): tools/test_replicate.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST8_BIN): tools/test_communicate.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST9_BIN): tools/test_events.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST10_BIN): tools/test_society.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST11_BIN): tools/test_agent_llm.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

$(TEST12_BIN): tools/test_scenario.o $(CORE_OBJ)
	$(CC) $(CFLAGS) -o $@ $^ $(LDFLAGS)

src/%.o: src/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

tools/%.o: tools/%.c
	$(CC) $(CFLAGS) -c -o $@ $<

clean:
	rm -f src/*.o tools/*.o $(BIN) $(VISUAL_BIN) $(TEST1_BIN) $(TEST2_BIN) $(TEST3_BIN) $(TEST4_BIN) $(TEST5_BIN) $(TEST6_BIN) $(TEST7_BIN) $(TEST8_BIN) $(TEST9_BIN) $(TEST10_BIN) $(TEST11_BIN) $(TEST12_BIN) universe.db

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
