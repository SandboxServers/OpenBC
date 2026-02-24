# OpenBC Cross-Platform Portability Guide

**Hard rule: all code must build and run on Windows, Linux, and macOS.**
If something works on 2 of 3, fix the third. If a genuine hard blocker exists,
file a GitHub issue rather than silently dropping support.

## Platform Detection

```c
#ifdef _WIN32
    /* Windows (MinGW cross-compile from WSL2, or native MSVC) */
#elif defined(__APPLE__)
    /* macOS (Apple Clang) */
#else
    /* Linux (GCC or Clang) */
#endif
```

Use `_WIN32` for Windows-vs-POSIX splits. Only add `__APPLE__` when macOS
diverges from Linux (it does more often than you'd expect).

In Makefiles, the `PLATFORM` variable maps `uname -s` output:
- `Linux` -> Linux
- `Darwin` -> macOS
- `Windows` -> Win32 cross-compile (or native MinGW)

## Encapsulate, Don't Scatter

Never put `#ifdef` blocks inside business logic. Wrap platform differences in
small static helper functions:

```c
static void *dl_open(const char *path) {
#ifdef _WIN32
    return (void *)LoadLibraryA(path);
#else
    return dlopen(path, RTLD_NOW | RTLD_LOCAL);
#endif
}
```

One function, one platform split, tested once. Business logic calls `dl_open()`.

## Master Divergence Table

| Area | Windows (MinGW) | Linux (GCC) | macOS (Clang) | Notes |
|------|-----------------|-------------|---------------|-------|
| **DLL extension** | `.dll` | `.so` | **`.dylib`** | macOS does NOT use `.so` |
| **DLL loading** | `LoadLibraryA`/`FreeLibrary` | `dlopen`/`dlclose` | `dlopen`/`dlclose` | Same API on macOS/Linux |
| **DLL link flag** | (none) | `-ldl` | **(none)** | macOS: dlopen is in libSystem |
| **DLL export** | `__declspec(dllexport)` | `__attribute__((visibility("default")))` | same as Linux | |
| **Sockets** | `winsock2.h`, `WSAStartup` | `sys/socket.h` | `sys/socket.h` | |
| **Socket link** | `-lws2_32` | (none) | (none) | |
| **Sleep** | `Sleep(ms)` | `usleep(us)` | `usleep(us)` | |
| **Monotonic clock** | `GetTickCount()` | `clock_gettime(MONOTONIC)` | `clock_gettime(MONOTONIC)` | macOS 10.12+ |
| **Path separator** | `\` (also `/`) | `/` | `/` | Accept both in validation |
| **Console signal** | `SetConsoleCtrlHandler` | `sigaction` | `sigaction` | |
| **Error formatting** | `FormatMessageA` | `dlerror()` / `strerror()` | `dlerror()` / `strerror()` | |
| **Stack probing** | Missing `__chkstk` in MinGW | N/A | N/A | See CLAUDE.md |
| **C++ linker** | `i686-w64-mingw32-g++` | `c++` | `c++` | bgfx needs C++ link |
| **Rendering backend** | D3D11/D3D12 | OpenGL/Vulkan | **Metal** | bgfx auto-selects |
| **Audio backend** | WASAPI | ALSA/PulseAudio | **Core Audio** | miniaudio handles this |
| **Lua platform define** | `LUA_USE_WINDOWS` | `LUA_USE_LINUX` | **`LUA_USE_MACOSX`** | Different per OS |
| **pkg-config** | `i686-w64-mingw32-pkg-config` | `pkg-config` | `pkg-config` | MinGW needs wrapper |

## The macOS `.dylib` Gotcha

This is the single most common cross-platform bug. macOS shared libraries use
`.dylib`, not `.so`. Any code that validates or constructs DLL paths must
three-way branch:

```c
#ifdef _WIN32
    const char *ext = ".dll";
#elif defined(__APPLE__)
    const char *ext = ".dylib";
#else
    const char *ext = ".so";
#endif
```

## The macOS `-ldl` Gotcha

On macOS, `dlopen`/`dlsym`/`dlclose` live in `libSystem` (always linked).
Passing `-ldl` may warn or fail on some macOS toolchains. The Makefile must
three-way branch:

```makefile
ifeq ($(PLATFORM),Windows)
    DL_LIBS :=
else ifeq ($(PLATFORM),Darwin)
    DL_LIBS :=
else
    DL_LIBS := -ldl
endif
```

## Socket Differences

Windows Winsock requires `WSAStartup()` / `WSACleanup()`. POSIX does not.
The codebase already handles this in `bc_net_init()` / `bc_net_shutdown()`.

Key divergences within socket code:
- `closesocket()` (Windows) vs `close()` (POSIX)
- `SOCKET` type (Windows, unsigned) vs `int` (POSIX, signed, -1 = error)
- `WSAEWOULDBLOCK` (Windows) vs `EAGAIN`/`EWOULDBLOCK` (POSIX)
- `ioctlsocket()` (Windows) vs `fcntl()` (POSIX) for non-blocking
- `MSG_NOSIGNAL` exists on Linux but **NOT on macOS**; use
  `setsockopt(SO_NOSIGPIPE)` on macOS or `signal(SIGPIPE, SIG_IGN)` globally

## Path Handling

- Always accept both `/` and `\` as separators in path validation
- Never hardcode path separators; use both in traversal checks
- macOS filesystem is case-insensitive by default; Linux is case-sensitive
  - `#include "OpenBC/Config.h"` works on macOS, fails on Linux
  - Always match exact case in `#include` directives

## CI Coverage

Azure Pipelines builds all three platforms. Rules:
- Never merge if any CI leg is red
- If a platform-specific failure occurs, fix it in the same PR
- If it's a genuine blocker that can't be fixed now, file a GitHub issue and
  add a `TODO(platform)` comment in code referencing the issue number

## Preprocessor Indent Convention

Nested preprocessor directives use hash + 2 spaces:
```c
#ifdef _WIN32
#  include <windows.h>
#else
#  include <dlfcn.h>
#endif
```
