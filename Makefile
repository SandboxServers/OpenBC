# OpenBC Makefile
# Supports native Linux/macOS builds and cross-compile to Win32 via MinGW.
# Cross-compile:  make PLATFORM=Windows
# Native:         make  (or make PLATFORM=Linux / make PLATFORM=Darwin)

PLATFORM ?= $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(PLATFORM),Windows)
    CC       := i686-w64-mingw32-gcc
    EXE      := .exe
    NET_LIBS := -lws2_32
    POSIX_DEFS :=
else
    CC       := cc
    EXE      :=
    NET_LIBS :=
    # Expose POSIX.1-2008 + BSD extensions (getaddrinfo, usleep, opendir, DT_*)
    POSIX_DEFS := -D_DEFAULT_SOURCE
endif

CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g -O2 $(POSIX_DEFS)
LDFLAGS  :=
LDLIBS   := -lm

# Build directory
BUILD    := build

# Source files by component
CHECKSUM_SRC := src/checksum/string_hash.c src/checksum/file_hash.c src/checksum/hash_tables.c src/checksum/manifest.c
PROTOCOL_SRC := src/protocol/cipher.c src/protocol/buffer.c src/protocol/opcodes.c src/protocol/handshake.c src/protocol/game_events.c src/protocol/game_builders.c
NETWORK_SRC  := src/network/net.c src/network/peer.c src/network/transport.c src/network/gamespy.c src/network/reliable.c src/network/master.c src/network/client_transport.c
JSON_SRC     := src/json/json_parse.c
GAME_SRC     := src/game/ship_data.c src/game/ship_state.c src/game/ship_power.c src/game/movement.c src/game/combat.c src/game/torpedo_tracker.c
MANIFEST_SRC := tools/manifest.c
LOG_SRC      := src/server/log.c
SERVER_SRC   := src/server/main.c src/server/server_state.c \
                src/server/server_send.c src/server/server_handshake.c \
                src/server/server_dispatch.c src/server/server_stats.c

# Object files
CHECKSUM_OBJ := $(CHECKSUM_SRC:%.c=$(BUILD)/%.o)
PROTOCOL_OBJ := $(PROTOCOL_SRC:%.c=$(BUILD)/%.o)
NETWORK_OBJ  := $(NETWORK_SRC:%.c=$(BUILD)/%.o)
JSON_OBJ     := $(JSON_SRC:%.c=$(BUILD)/%.o)
GAME_OBJ     := $(GAME_SRC:%.c=$(BUILD)/%.o)
MANIFEST_OBJ := $(MANIFEST_SRC:%.c=$(BUILD)/%.o)
LOG_OBJ      := $(LOG_SRC:%.c=$(BUILD)/%.o)
SERVER_OBJ   := $(SERVER_SRC:%.c=$(BUILD)/%.o)

# All library objects (everything except tools and server main)
LIB_OBJ      := $(CHECKSUM_OBJ) $(PROTOCOL_OBJ) $(NETWORK_OBJ) $(JSON_OBJ) $(GAME_OBJ) $(LOG_OBJ)

# Test files
TEST_SRC     := $(wildcard tests/test_*.c)
TEST_BIN     := $(TEST_SRC:tests/%.c=$(BUILD)/tests/%$(EXE))

# Targets
.PHONY: all clean test server

all: $(BUILD)/openbc-hash$(EXE) $(BUILD)/openbc-server$(EXE)

# --- Hash manifest tool ---
$(BUILD)/openbc-hash$(EXE): $(CHECKSUM_OBJ) $(PROTOCOL_OBJ) $(JSON_OBJ) $(LOG_OBJ) $(MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# --- Server binary ---
server: $(BUILD)/openbc-server$(EXE)

$(BUILD)/openbc-server$(EXE): $(LIB_OBJ) $(SERVER_OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(NET_LIBS)

# --- Test runner ---
test: $(TEST_BIN)
	@echo "=== Running tests ==="
	@pass=0; fail=0; \
	for t in $(TEST_BIN); do \
		printf "  %-40s " "$$(basename $$t)"; \
		if $$t > /dev/null 2>&1; then \
			echo "PASS"; pass=$$((pass+1)); \
		else \
			echo "FAIL"; $$t; fail=$$((fail+1)); \
		fi; \
	done; \
	echo "=== $$pass passed, $$fail failed ===";\
	[ $$fail -eq 0 ]

# Each test binary links against all library objects.
# Tests use -O1 to avoid i686-w64-mingw32-gcc -O2 dead-store bugs that occur
# when a test .c is compiled separately from the library .o files.
$(BUILD)/tests/test_%$(EXE): tests/test_%.c $(LIB_OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -O1 $(LDFLAGS) -o $@ $^ $(LDLIBS) $(NET_LIBS)

# --- Generic object compilation ---
$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Copy data files into build directory ---
$(BUILD)/data: data
	@mkdir -p $(BUILD)/data
	cp -u data/* $(BUILD)/data/ 2>/dev/null || true

all: $(BUILD)/data

# --- Clean ---
# Removes build artifacts but preserves server log files (*.log) and data/
clean:
	@find $(BUILD) -path '$(BUILD)/data' -prune -o -type f ! -name '*.log' -exec rm -f {} + 2>/dev/null; \
	find $(BUILD) -path '$(BUILD)/data' -prune -o -type d -empty -exec rmdir {} + 2>/dev/null; \
	true
