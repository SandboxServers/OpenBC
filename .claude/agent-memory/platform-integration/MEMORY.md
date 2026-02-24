# Platform Integration Agent Memory

## Key Learnings

### Module API ABI Concerns
- Variadic function pointers (`void (*log_info)(const char *fmt, ...)`) in vtables are an ABI hazard on i686 Win32 when DLL and host use different calling conventions. Safe when both use `__cdecl` (GCC default), unsafe if module compiled with `/Gz` (__stdcall).
- Recommendation: annotate with `__cdecl` on Win32, or change to non-variadic `void (*log_info)(const char *msg)`.
- See [module-api-review.md](module-api-review.md) for details.

### Config System
- `strtod()` is locale-sensitive. Config stores float values as strings (via `"%.17g"`), but reads them back with `strtod()`. On non-C locales with `,` decimal separator, this silently fails. Low priority for game servers but worth tracking.
- Vendored toml-c uses `_POSIX_C_SOURCE 200809L` for `strnlen`. Works on MinGW, glibc, MSVC. No action needed.
- Config paths stored as raw strings -- no backslash/forward-slash translation across platforms.

### Build System
- `-Isrc` added to CFLAGS for vendored `toml/toml.h` include resolution.
- Event bus and TOML parser objects added to `SERVER_LIB_OBJ` and linked into test binaries via `LIB_OBJ`.

## File Paths
- Module API header: `include/openbc/module_api.h`
- Event bus: `include/openbc/event_bus.h`, `src/server/event_bus.c`
- Config system: `include/openbc/config.h`, `src/server/config.c`
- Vendored TOML: `src/toml/toml.c`, `src/toml/toml.h`
- Example config: `server.toml`
