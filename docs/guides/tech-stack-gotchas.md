# OpenBC Tech Stack Gotchas

Per-dependency pitfalls for the OpenBC tech stack. Each section covers a
dependency we use (or will use) and the platform-specific issues to watch for.

## C11 (Core Language)

Covered in [coding-patterns.md](coding-patterns.md). Key reminders:
- `-std=c11 -Wall -Wextra -Wpedantic` -- zero warnings policy
- No VLAs, no `typeof`, no statement expressions (GCC extensions)
- Function/object pointer casts forbidden -- use `memcpy` type-pun
- `snprintf` everywhere, never `strcpy`/`strcat`/`sprintf`

## C++ (bgfx Link Step, Future Modules)

### Compiler selection
- Linux: `c++` (usually g++ or clang++)
- macOS: `c++` (Apple Clang -- does NOT support `__float128` or some GNU extensions)
- Windows: `i686-w64-mingw32-g++` (MinGW cross-compile from WSL2)

### Mixing C and C++
All public C headers must use `extern "C"` guards:
```c
#ifdef __cplusplus
extern "C" {
#endif

/* ... declarations ... */

#ifdef __cplusplus
}
#endif
```
Without these, C++ name mangling makes symbols invisible to C code.

### ABI pitfalls
- `bool` size/alignment differs between C and C++ on some compilers. Use `int`
  (0/1) at the C/C++ boundary (same rule as DLL boundaries).
- `enum` underlying type may differ. Use explicit-width integers at boundaries.
- Exceptions must NOT propagate across C/C++ boundaries (undefined behavior).
  Use `noexcept` on all C-callable C++ functions, or wrap in try/catch.

### Apple Clang vs GCC/MinGW
- Apple Clang uses libc++ by default; Linux GCC uses libstdc++
- `-stdlib=libc++` is implicit on macOS; never pass `-stdlib=libstdc++` there
- Apple Clang may lag behind C++20/23 features vs GCC. Stick to C++17 max.
- `__attribute__((visibility("default")))` works on both but syntax differs
  from MSVC `__declspec(dllexport)`. Use a macro.

## bgfx (Rendering)

### Backend selection per platform
| Platform | Primary | Fallback | Notes |
|----------|---------|----------|-------|
| Windows  | D3D11   | D3D12, OpenGL | D3D11 most stable on MinGW |
| macOS    | Metal   | OpenGL (deprecated) | macOS deprecated OpenGL in 10.14 |
| Linux    | OpenGL  | Vulkan | Vulkan requires driver support |

bgfx auto-selects at runtime. Do NOT hardcode a renderer type unless testing.

### Shader compilation
bgfx uses `shaderc` to compile shaders to platform-specific bytecode:
- HLSL -> D3D (Windows)
- Metal Shading Language -> Metal (macOS)
- GLSL/SPIR-V -> OpenGL/Vulkan (Linux)

Shaders must be compiled per-platform. Ship all three variants or compile at
build time per target. `shaderc` is a build-time tool, not runtime.

### C++ link requirement
bgfx is C++ internally. The final link step for any binary using bgfx must use
the C++ linker (`$(CXX)`), not `$(CC)`. The Makefile already handles this via
`CLIENT_LINK := $(CXX)` when `CLIENT_BACKEND=sdl3-bgfx`.

### bgfx initialization
- `bgfx::init()` must be called from the main thread
- On macOS, the Metal layer must be set up on the main thread (CAMetalLayer)
- Window handle passing differs per platform:
  - Windows: `HWND`
  - macOS: `NSWindow*` (or `CAMetalLayer*`)
  - Linux: `Window` (X11) or `wl_surface*` (Wayland)
- bgfx `platformData` struct fields differ per OS -- use `#ifdef` to fill

### bgfx memory
- `bgfx::makeRef()` does NOT copy data -- the buffer must outlive the frame
- `bgfx::copy()` copies data -- safe but slower
- Double-buffered: `bgfx::frame()` swaps, previous frame's makeRef buffers
  are safe to free after next `frame()` call

## SDL3 (Window/Input)

### Build system
- SDL3 uses CMake. Prefer `pkg-config` for finding it:
  ```makefile
  SDL3_CFLAGS := $(shell pkg-config --cflags sdl3)
  SDL3_LIBS   := $(shell pkg-config --libs sdl3)
  ```
- For MinGW cross-compile, use `i686-w64-mingw32-pkg-config` or pass
  `SDL3_CFLAGS`/`SDL3_LIBS` manually
- On macOS, SDL3 may be installed via Homebrew -- `pkg-config` path must
  include `/opt/homebrew/lib/pkgconfig` on Apple Silicon

### SDL3 vs SDL2 migration pitfalls
- `SDL_Init()` returns `bool` in SDL3, not `int` (SDL2 returned 0 on success)
- `SDL_CreateWindow()` no longer takes x/y position -- use
  `SDL_SetWindowPosition()` after creation
- `SDL_Event` union layout changed -- some field offsets differ
- `SDL_RENDERER_*` flags removed -- SDL3 uses properties instead
- `SDL_GetTicks()` returns `Uint64` (ms) in SDL3, was `Uint32` in SDL2

### Platform-specific SDL3 issues
- **macOS**: SDL3 requires the main thread for event processing and window
  creation. Use `SDL_main` or call from `main()` directly.
- **Linux/Wayland**: SDL3 defaults to Wayland if available. Set
  `SDL_VIDEO_DRIVER=x11` env var to force X11 if needed.
- **Windows/MinGW**: Link against `-lmingw32 -lSDL3main -lSDL3` in that order.
  Order matters for MinGW's linker.

## Lua 5.4 (Scripting)

### Platform defines
Each platform needs a different define for Lua's OS-specific features:
```makefile
ifeq ($(PLATFORM),Windows)
    LUA_DEFS := -DLUA_USE_WINDOWS
else ifeq ($(PLATFORM),Darwin)
    LUA_DEFS := -DLUA_USE_MACOSX
else
    LUA_DEFS := -DLUA_USE_LINUX
endif
```
Getting these wrong causes missing features (e.g., `os.execute`, `io.popen`).

### Embedding pitfalls
- **Stack discipline**: Every `lua_push*` must be balanced by a pop or consumed
  by a call. Leaking stack slots is the #1 embedding bug. Use `lua_gettop()`
  before/after to assert balance.
- **Error handling**: Lua errors longjmp. In C, this skips cleanup code. Always
  use `lua_pcall()` / `lua_pcallk()`, never `lua_call()` in production code.
  In C++, longjmp is even worse -- it skips destructors -> UB.
- **String lifetime**: `lua_tostring()` returns a pointer valid only while the
  string is on the Lua stack. Copy with `snprintf` before popping.
- **Integer vs number**: Lua 5.4 has distinct integer (64-bit) and float
  (double) types. Use `lua_isinteger()` to distinguish. `lua_tonumber()` may
  silently truncate integers to double.
- **Registry keys**: Use `luaL_ref()` / `luaL_unref()` for C-side references
  to Lua objects. Never use raw integer keys in the registry.

### Lua C API and C++ interop
If compiling Lua as C (default) but calling from C++, the Lua headers must be
wrapped in `extern "C"`:
```cpp
extern "C" {
#include <lua.h>
#include <lauxlib.h>
#include <lualib.h>
}
```
Alternatively, compile Lua itself as C++ (`-x c++`), but this changes error
handling from longjmp to exceptions -- pick one and be consistent.

### Sandbox restrictions
Module Lua scripts run in a sandbox. The sandbox must:
- Remove `os.execute`, `io.popen`, `io.open`, `loadfile`, `dofile`
- Remove `debug.getinfo`, `debug.sethook` (or whitelist specific uses)
- Set memory limits via `lua_setallocf` with a counting allocator
- Set instruction limits via `lua_sethook` with `LUA_MASKCOUNT`
- Provide only the whitelisted engine API via the module table

### Lua on Windows (MinGW)
- Lua builds cleanly with MinGW, but `LUA_USE_WINDOWS` must be defined
- `lua_writestring` and `lua_writeline` may need `fflush(stdout)` on Windows
  (buffered console I/O)
- `LUA_USE_DLOPEN` is for POSIX only -- Windows uses `LoadLibrary` internally

## toml-c (Configuration)

### Already integrated
toml-c is vendored at `src/toml/toml.c`. Minimal gotchas:
- Returns `malloc`'d strings -- caller must `free()` after use
- Parse errors returned as struct with line/column -- always check and log
- Thread safety: parsing is stateless, safe for concurrent reads
- No platform-specific issues (pure C99, no system calls)

## cglm (Math)

### Header-only by default
cglm can be used header-only (`#include <cglm/cglm.h>`) or compiled as a
library. Header-only is simpler and avoids link issues.

### SIMD and platform differences
- x86 (Windows/Linux): Uses SSE2 by default, SSE4.1 with `-msse4.1`
- ARM (macOS Apple Silicon): Uses NEON -- cglm handles this automatically
  via `#ifdef __ARM_NEON`
- If cross-compiling for 32-bit x86 (MinGW i686): SSE2 available but SSE4
  may not be. Stick to default SSE2 or use scalar fallback.

### Float precision
cglm uses `float` (32-bit) exclusively, not `double`. This matches bgfx and
game engines generally. Don't mix `double` math into cglm pipelines.

## miniaudio (Audio)

### Backend selection
miniaudio auto-selects the audio backend:
| Platform | Backend | Notes |
|----------|---------|-------|
| Windows  | WASAPI  | Preferred; also supports DirectSound |
| macOS    | Core Audio | Via AudioUnit API |
| Linux    | PulseAudio -> ALSA | Falls back to ALSA if no PulseAudio |

### Single-file library
miniaudio is a single header (`miniaudio.h`). Define `MA_IMPLEMENTATION` in
exactly ONE `.c` file:
```c
#define MA_IMPLEMENTATION
#include "miniaudio.h"
```
Multiple definitions -> duplicate symbol errors at link time.

### Threading
miniaudio creates its own audio thread for playback/capture. Callbacks fire
on this thread -- use atomics or mutexes for shared state, never Lua calls
(not thread-safe).

### macOS specific
- Core Audio requires the `AudioToolbox` and `CoreAudio` frameworks.
  Link with `-framework AudioToolbox -framework CoreAudio` on macOS.
- On macOS, miniaudio may need microphone permission (TCC) for capture.

## Dear ImGui (Dev Tools)

### Integration with bgfx
Dear ImGui needs a renderer backend. For bgfx, use `imgui_impl_bgfx` or
the bgfx examples' built-in imgui integration. Do NOT mix with SDL_Renderer.

### Platform backend
Use `imgui_impl_sdl3` for input/window events. This pairs with SDL3 for
window management and bgfx for rendering.

### C++ requirement
Dear ImGui is C++. Same rules as bgfx:
- Final link with `$(CXX)`
- If calling from C code, wrap ImGui functions in `extern "C"` C++ wrappers
- ImGui uses `new`/`delete` internally -- ensure C++ runtime is linked

### Font rendering
- ImGui uses a built-in font atlas. Custom fonts need `ImFontAtlas::AddFont*`
  before `ImGui::NewFrame()`.
- On high-DPI displays (macOS Retina, Windows scaling), multiply font size by
  `SDL_GetWindowDisplayScale()` or similar. Otherwise text is tiny.

## pkg-config (Build Tool)

### Cross-compilation
When cross-compiling with MinGW, system `pkg-config` finds host libraries,
not target libraries. Solutions:
1. Set `PKG_CONFIG=i686-w64-mingw32-pkg-config` (if installed)
2. Pass `*_CFLAGS` and `*_LIBS` manually (current Makefile approach)
3. Set `PKG_CONFIG_LIBDIR` to the MinGW sysroot's pkgconfig directory

### macOS Homebrew
On macOS with Homebrew, pkg-config files may be in non-standard paths:
- Intel Mac: `/usr/local/lib/pkgconfig`
- Apple Silicon: `/opt/homebrew/lib/pkgconfig`

Set `PKG_CONFIG_PATH` accordingly, or use `brew --prefix <pkg>`.
