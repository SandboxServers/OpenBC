# bgfx Renderer Agent Memory

## Project State
- Phase 2 Track B (client rendering) started with PR #171: SDL3 + bgfx bootstrap
- Backend abstraction: `src/client/client_backend.h` with noop and sdl3-bgfx implementations
- Build: `make client CLIENT_BACKEND=sdl3-bgfx` (requires SDL3 + bgfx libs)
- Default build uses noop backend (no graphics deps)

## bgfx C99 API Patterns
- See [bgfx-api-patterns.md](bgfx-api-patterns.md) for detailed notes

## Key File Paths
- Client backend header: `src/client/client_backend.h`
- SDL3+bgfx backend: `src/client/client_backend_sdl3_bgfx.c`
- Noop backend: `src/client/client_backend_noop.c`
- Client main: `src/client/main.c`

## Architecture Decisions
- Renderer type: D3D11 (Windows), Metal (macOS), Vulkan (Linux) with fallback enabled
- Platform data: Currently uses `bgfx_set_platform_data()` global; should migrate to `init.platformData`
- HiDPI: Uses `SDL_WINDOW_HIGH_PIXEL_DENSITY` + `SDL_GetWindowSizeInPixels()`
- Resize: Should use ONLY `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` (not `SDL_EVENT_WINDOW_RESIZED`)
