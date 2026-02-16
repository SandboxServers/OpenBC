# OpenBC Makefile
# Cross-compiles from WSL2 to Win32 using mingw.
# WSL2 runs Windows .exe natively, so tests work directly.

CC       := i686-w64-mingw32-gcc
CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -Iinclude -g -O2
LDFLAGS  :=
LDLIBS   := -lm
NET_LIBS := -lws2_32
EXE      := .exe

# Build directory
BUILD    := build

# Source files by component
CHECKSUM_SRC := src/checksum/string_hash.c src/checksum/file_hash.c src/checksum/hash_tables.c src/checksum/manifest.c
PROTOCOL_SRC := src/protocol/cipher.c src/protocol/buffer.c src/protocol/opcodes.c src/protocol/handshake.c
NETWORK_SRC  := src/network/net.c src/network/peer.c src/network/transport.c src/network/gamespy.c src/network/reliable.c src/network/master.c
JSON_SRC     := src/json/json_parse.c
MANIFEST_SRC := tools/manifest.c
SERVER_SRC   := src/server/main.c

# Object files
CHECKSUM_OBJ := $(CHECKSUM_SRC:%.c=$(BUILD)/%.o)
PROTOCOL_OBJ := $(PROTOCOL_SRC:%.c=$(BUILD)/%.o)
NETWORK_OBJ  := $(NETWORK_SRC:%.c=$(BUILD)/%.o)
JSON_OBJ     := $(JSON_SRC:%.c=$(BUILD)/%.o)
MANIFEST_OBJ := $(MANIFEST_SRC:%.c=$(BUILD)/%.o)
SERVER_OBJ   := $(SERVER_SRC:%.c=$(BUILD)/%.o)

# All library objects (everything except tools and server main)
LIB_OBJ      := $(CHECKSUM_OBJ) $(PROTOCOL_OBJ) $(NETWORK_OBJ) $(JSON_OBJ)

# Test files
TEST_SRC     := $(wildcard tests/test_*.c)
TEST_BIN     := $(TEST_SRC:tests/%.c=$(BUILD)/tests/%$(EXE))

# Targets
.PHONY: all clean test server

all: $(BUILD)/openbc-hash$(EXE) $(BUILD)/openbc-server$(EXE)

# --- Hash manifest tool ---
$(BUILD)/openbc-hash$(EXE): $(CHECKSUM_OBJ) $(JSON_OBJ) $(MANIFEST_OBJ)
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

# Each test binary links against all library objects
$(BUILD)/tests/test_%$(EXE): tests/test_%.c $(LIB_OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(NET_LIBS)

# --- Generic object compilation ---
$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -c -o $@ $<

# --- Clean ---
clean:
	rm -rf $(BUILD)
