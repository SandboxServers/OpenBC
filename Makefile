# OpenBC Makefile
# Supports native Linux/macOS builds and cross-compile to Win32 via MinGW.
# Cross-compile:  make PLATFORM=Windows
# Native:         make  (or make PLATFORM=Linux / make PLATFORM=Darwin)

PLATFORM ?= $(shell uname -s 2>/dev/null || echo Windows)

ifeq ($(PLATFORM),Windows)
    CC       := i686-w64-mingw32-gcc
    CXX      := i686-w64-mingw32-g++
    EXE      := .exe
    NET_LIBS := -lws2_32
    DL_LIBS  :=
    POSIX_DEFS :=
else
    CC       := cc
    CXX      := c++
    EXE      :=
    NET_LIBS :=
    # macOS: dlopen lives in libSystem (always linked); Linux needs -ldl
    ifeq ($(PLATFORM),Darwin)
        DL_LIBS :=
    else
        DL_LIBS := -ldl
    endif
    # Expose POSIX.1-2008 + BSD extensions (getaddrinfo, usleep, opendir, DT_*)
    POSIX_DEFS := -D_DEFAULT_SOURCE
endif

CFLAGS   := -std=c11 -Wall -Wextra -Wpedantic -Iinclude -Isrc -g -O2 $(POSIX_DEFS)
DEPFLAGS  = -MMD -MP -MF $(@:.o=.d)
LDFLAGS  :=
LDLIBS   := -lm
# When cross-compiling (PLATFORM=Windows), use a MinGW-targeted pkg-config
# wrapper (for example, PKG_CONFIG=i686-w64-mingw32-pkg-config), or provide
# SDL3_CFLAGS/SDL3_LIBS/BGFX_LIBS manually.
PKG_CONFIG ?= pkg-config

# Build directory
BUILD    := build

# Source files by component
CHECKSUM_SRC := src/shared/checksum/string_hash.c src/shared/checksum/file_hash.c src/shared/checksum/hash_tables.c src/shared/checksum/manifest.c
PROTOCOL_SRC := src/shared/protocol/cipher.c src/shared/protocol/buffer.c src/shared/protocol/opcodes.c src/shared/protocol/handshake.c src/shared/protocol/game_events.c src/shared/protocol/game_builders.c src/shared/protocol/client_transport.c
SERVER_NET_SRC := src/server/network/net.c src/server/network/peer.c src/server/network/transport.c src/server/network/gamespy.c src/server/network/reliable.c src/server/network/master.c
JSON_SRC     := src/shared/json/json_parse.c
GAME_SRC     := src/shared/game/ship_data.c src/shared/game/ship_state.c src/shared/game/ship_power.c src/shared/game/movement.c src/shared/game/combat.c src/shared/game/torpedo_tracker.c
MANIFEST_SRC := tools/manifest.c
TOML_SRC     := src/toml/toml.c
CONFIG_SRC   := src/server/config.c
LOG_SRC      := src/server/log.c
EVENT_BUS_SRC := src/server/event_bus.c
MODULE_LOADER_SRC := src/server/module_loader.c
SERVER_SRC   := src/server/main.c src/server/server_state.c \
                src/server/server_send.c src/server/server_handshake.c \
                src/server/server_dispatch.c src/server/server_stats.c \
                $(MODULE_LOADER_SRC)

CLIENT_BACKEND ?= noop
SDL3_CFLAGS ?=
SDL3_LIBS ?=
BGFX_CFLAGS ?=
BGFX_LIBS ?=
CLIENT_CFLAGS :=
CLIENT_LDLIBS :=
CLIENT_LINK := $(CC)
CLIENT_SRC := src/client/main.c
SDL3_CFLAGS_RESOLVED :=
SDL3_LIBS_RESOLVED :=

ifeq ($(CLIENT_BACKEND),sdl3-bgfx)
CLIENT_SRC += src/client/client_backend_sdl3_bgfx.c
ifneq ($(strip $(SDL3_CFLAGS)),)
SDL3_CFLAGS_RESOLVED := $(SDL3_CFLAGS)
else
SDL3_CFLAGS_RESOLVED := $(shell $(PKG_CONFIG) --cflags sdl3 2>/dev/null)
endif
ifneq ($(strip $(SDL3_LIBS)),)
SDL3_LIBS_RESOLVED := $(SDL3_LIBS)
else
SDL3_LIBS_RESOLVED := $(shell $(PKG_CONFIG) --libs sdl3 2>/dev/null)
endif
CLIENT_CFLAGS += $(SDL3_CFLAGS_RESOLVED) $(BGFX_CFLAGS) -DOPENBC_CLIENT_SDL3_BGFX
CLIENT_LDLIBS += $(SDL3_LIBS_RESOLVED) $(BGFX_LIBS)
CLIENT_LINK := $(CXX)
else ifeq ($(CLIENT_BACKEND),noop)
CLIENT_SRC += src/client/client_backend_noop.c
else
$(error Unsupported CLIENT_BACKEND='$(CLIENT_BACKEND)'. Use 'noop' or 'sdl3-bgfx')
endif

# Object files
CHECKSUM_OBJ := $(CHECKSUM_SRC:%.c=$(BUILD)/%.o)
PROTOCOL_OBJ := $(PROTOCOL_SRC:%.c=$(BUILD)/%.o)
SERVER_NET_OBJ := $(SERVER_NET_SRC:%.c=$(BUILD)/%.o)
JSON_OBJ     := $(JSON_SRC:%.c=$(BUILD)/%.o)
GAME_OBJ     := $(GAME_SRC:%.c=$(BUILD)/%.o)
MANIFEST_OBJ := $(MANIFEST_SRC:%.c=$(BUILD)/%.o)
TOML_OBJ     := $(TOML_SRC:%.c=$(BUILD)/%.o)
CONFIG_OBJ   := $(CONFIG_SRC:%.c=$(BUILD)/%.o)
LOG_OBJ      := $(LOG_SRC:%.c=$(BUILD)/%.o)
EVENT_BUS_OBJ := $(EVENT_BUS_SRC:%.c=$(BUILD)/%.o)
MODULE_LOADER_OBJ := $(MODULE_LOADER_SRC:%.c=$(BUILD)/%.o)
SERVER_OBJ   := $(SERVER_SRC:%.c=$(BUILD)/%.o)
CLIENT_OBJ   := $(CLIENT_SRC:%.c=$(BUILD)/%.o)

# All library objects (everything except tools and server main)
SHARED_OBJ   := $(CHECKSUM_OBJ) $(PROTOCOL_OBJ) $(JSON_OBJ) $(GAME_OBJ) $(LOG_OBJ)
SERVER_LIB_OBJ := $(SHARED_OBJ) $(SERVER_NET_OBJ) $(EVENT_BUS_OBJ) $(TOML_OBJ) $(CONFIG_OBJ)
LIB_OBJ      := $(SERVER_LIB_OBJ)

# Test files
TEST_SRC     := $(wildcard tests/test_*.c)
TEST_BIN     := $(TEST_SRC:tests/%.c=$(BUILD)/tests/%$(EXE))

# Targets
.PHONY: all clean test server client check-client-config

all: $(BUILD)/openbc-hash$(EXE) $(BUILD)/openbc-server$(EXE) $(BUILD)/openbc-client$(EXE)

# --- Hash manifest tool ---
$(BUILD)/openbc-hash$(EXE): $(CHECKSUM_OBJ) $(PROTOCOL_OBJ) $(JSON_OBJ) $(LOG_OBJ) $(MANIFEST_OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS)

# --- Server binary ---
server: $(BUILD)/openbc-server$(EXE)

$(BUILD)/openbc-server$(EXE): $(SERVER_LIB_OBJ) $(SERVER_OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(LDFLAGS) -o $@ $^ $(LDLIBS) $(NET_LIBS) $(DL_LIBS)

# --- Client binary ---
client: $(BUILD)/openbc-client$(EXE)

check-client-config:
ifeq ($(CLIENT_BACKEND),sdl3-bgfx)
	@if [ -z "$(strip $(SDL3_CFLAGS_RESOLVED))" ] || [ -z "$(strip $(SDL3_LIBS_RESOLVED))" ]; then \
		echo "CLIENT_BACKEND=sdl3-bgfx: SDL3 not found via pkg-config. Set both SDL3_CFLAGS and SDL3_LIBS manually."; \
		exit 1; \
	fi
	@if [ -z "$(strip $(BGFX_LIBS))" ]; then \
		echo "CLIENT_BACKEND=sdl3-bgfx: BGFX_LIBS must be set explicitly (bgfx has no pkg-config)."; \
		exit 1; \
	fi
else
	@true
endif

$(BUILD)/openbc-client$(EXE): check-client-config $(SHARED_OBJ) $(CLIENT_OBJ)
	@mkdir -p $(@D)
	$(CLIENT_LINK) $(CFLAGS) $(LDFLAGS) -o $@ $(SHARED_OBJ) $(CLIENT_OBJ) $(LDLIBS) $(CLIENT_LDLIBS)

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

# test_module_loader needs module_loader.o + server_state.o (provides globals)
# in addition to LIB_OBJ. It also needs DL_LIBS for dlopen/dlclose.
$(BUILD)/tests/test_module_loader$(EXE): tests/test_module_loader.c $(LIB_OBJ) $(MODULE_LOADER_OBJ) $(BUILD)/src/server/server_state.o
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -O1 $(LDFLAGS) -o $@ $^ $(LDLIBS) $(NET_LIBS) $(DL_LIBS)

# Each test binary links against all library objects.
# Tests use -O1 to avoid i686-w64-mingw32-gcc -O2 dead-store bugs that occur
# when a test .c is compiled separately from the library .o files.
$(BUILD)/tests/test_%$(EXE): tests/test_%.c $(LIB_OBJ)
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) -O1 $(LDFLAGS) -o $@ $^ $(LDLIBS) $(NET_LIBS)

# Apply backend-specific defines/includes only to client compilation units.
$(BUILD)/src/client/%.o: CFLAGS += $(CLIENT_CFLAGS)

# --- Generic object compilation ---
$(BUILD)/%.o: %.c
	@mkdir -p $(@D)
	$(CC) $(CFLAGS) $(DEPFLAGS) -c -o $@ $<

# Include auto-generated header dependencies
-include $(wildcard $(BUILD)/src/**/*.d $(BUILD)/tools/*.d)

# --- Copy data files into build directory ---
$(BUILD)/data: data
	@mkdir -p $(BUILD)/data
	cp -ru data/. $(BUILD)/data/ 2>/dev/null || true

all: $(BUILD)/data

# --- Clean ---
# Removes build artifacts but preserves server log files (*.log) and data/
clean:
	@find $(BUILD) -path '$(BUILD)/data' -prune -o -type f ! -name '*.log' -exec rm -f {} + 2>/dev/null; \
	find $(BUILD) -path '$(BUILD)/data' -prune -o -type d -empty -exec rmdir {} + 2>/dev/null; \
	true
