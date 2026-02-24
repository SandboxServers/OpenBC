# OpenBC Coding Patterns & Static Analyzer Guidance

Rules and patterns to follow so code passes Cppcheck / CodeRabbit on the first PR.

## Compiler Target

- C11 strict: `-std=c11 -Wall -Wextra -Wpedantic`
- Cross-compile: `i686-w64-mingw32-gcc` (Win32) or `cc` (POSIX)
- Tests compile at `-O1`; library code at `-O2`
- Zero warnings policy: every PR must build with zero warnings on all three platforms

## ISO C11 `-Wpedantic` Pitfalls

### Function/object pointer casts are forbidden
`dlsym` and `GetProcAddress` return through object-pointer-typed APIs, but ISO C
forbids direct casts between function and object pointers. Use `memcpy`:
```c
/* WRONG: -Wpedantic error */
obc_module_load_fn fn = (obc_module_load_fn)dlsym(handle, sym);

/* RIGHT: type-pun through memcpy */
void *sym = dlsym(handle, name);
obc_module_load_fn fn;
memcpy(&fn, &sym, sizeof(fn));
```
Same applies to `GetProcAddress` (FARPROC -> void*).

### No VLAs, no `typeof`, no statement expressions
These are GCC extensions, not C11. Use fixed-size arrays or `malloc`.

### No zero-length arrays
Use flexible array members (`type field[];`) at end of struct only, or fixed max.

## String Safety

### Always `snprintf`, never `strcpy`/`strcat`/`sprintf`
```c
snprintf(dst, sizeof(dst), "%s", src);  /* bounded copy */
```

### Format string safety -- no user data in format position
When forwarding variadic functions (e.g. module log wrappers calling `bc_log`),
`va_list` cannot be forwarded to another variadic function. Use the vsnprintf pattern:
```c
static void wrap_log_info(const char *fmt, ...) {
    char buf[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(buf, sizeof(buf), fmt, ap);
    va_end(ap);
    bc_log(LOG_INFO, "module", "%s", buf);  /* "%s" -- never buf as format */
}
```

## File-Scoped Statics Initialization

**Every public entry point that touches file-scoped statics must initialize ALL of them.**

CodeRabbit caught this on the first PR: `obc_module_api_build()` set `s_cfg` but
not `s_api_self`, creating a latent NULL deref if called independently of
`obc_module_loader_init()`. Rule: if a function populates a struct that wrappers
close over, set every file-scoped pointer in that function, not just some.

## Slot/Index Bounds Checking

Every function that takes a player slot must validate:
```c
if (slot < 0 || slot >= BC_MAX_PLAYERS) return 0;  /* or NULL, 0.f, etc. */
```
Same for subsystem indices, shield facings, array indices from external input.

## Unused Parameters

Silence with `(void)param;` on its own line. Never use `#pragma` to suppress.
```c
static void handler(const obc_engine_api_t *api, obc_event_ctx_t *ctx) {
    (void)api;
    (void)ctx;
    /* ... */
}
```

## const Correctness

Cppcheck flags parameters and locals that could be `const`. Apply `const` to:
- Pointer parameters that aren't modified: `const char *name`
- Pointer-to-struct parameters read-only: `const obc_server_cfg_t *cfg`
- Local pointers that aren't reassigned: `const bc_ship_class_t *cls = ...`

## Cppcheck `staticFunction` False Positive

Cppcheck flags functions as "should have static linkage" when they're defined in a
`.c` file but not called from other `.c` files in the same build. Functions exposed
in public headers for testability (e.g. `obc_module_path_validate`,
`obc_module_api_build`) will trigger this. This is expected -- the function IS used
externally by tests. No action needed.

## Resource Cleanup on Error Paths

Use the `goto fail` pattern with explicit per-resource cleanup before the jump:
```c
void *handle = dl_open(path);
if (!handle) { goto fail; }

sym = dl_sym(handle, name);
if (!sym) {
    dl_close(handle);  /* clean up THIS resource before jumping */
    goto fail;
}
/* ... */
fail:
    shutdown_all_loaded();  /* centralized cleanup of prior state */
    return -1;
```
Never leak a handle. If `dl_open` succeeded but a later step fails, close the handle
before `goto fail`.

## Cross-Platform: Three-OS Requirement

**All code must work on Windows, Linux, and macOS.** See
[cross-platform.md](cross-platform.md) for the full portability guide, master
divergence table, and macOS-specific gotchas.

## `bool` vs `int` at DLL Boundaries

The module API uses `int` (0/1) for boolean parameters and return values at the
DLL boundary. C11 `_Bool`/`bool` can have different ABI representations across
compilers. Internal code uses `bool` freely; the API table uses `int`.

## Test Patterns

### Custom Makefile link rules
Tests that need server-internal objects (like `module_loader.o` or `server_state.o`)
need an explicit Makefile rule BEFORE the generic `test_%` pattern rule. The custom
rule must list the extra `.o` files and any additional link libraries (e.g. `$(DL_LIBS)`).

### Stub globals for server state
Tests that link `module_loader.o` need the server globals resolved. Two approaches:
1. Link `server_state.o` directly (provides all globals with default values)
2. Define stub globals in the test file (more isolated but verbose)

Approach 1 is preferred unless the test needs to control initial values tightly.

### Stub functions for server subsystems
Functions from `.c` files not in `LIB_OBJ` (e.g. `server_send.c`) need stubs in
the test file:
```c
void bc_queue_reliable(int slot, const u8 *payload, int len) {
    (void)slot; (void)payload; (void)len;
}
```

## Known MinGW/GCC Gotchas

### GCC -O2 dead-store elimination
`i686-w64-mingw32-gcc -O2` silently drops stores after `memset()`. Use
`volatile u8 *` byte-copy loops. See `peer.c` for the workaround.

### Win32 stack probing
Large stack arrays (~4KB+) can skip the guard page on Win32 (no `__chkstk`).
Fix: write directly into output structs, avoid large locals.

## Things That Will NOT Trigger Warnings (Don't "Fix")

- Pre-existing `-Wunused-function` warnings in `test_harness.h` (static helpers
  not used by every test that includes the header). These are expected.
- `CRLF will be replaced by LF` git warnings on WSL2. The `.gitattributes` file
  handles normalization.
