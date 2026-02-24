# bgfx C99 API Patterns

## Initialization
- `bgfx_init_ctor(&init)` zeroes struct and sets safe defaults -- always call first
- `init.fallback = true` is a real C99 field: enables fallback to next renderer if preferred fails
- Prefer setting `init.platformData = pd` directly over calling `bgfx_set_platform_data(&pd)` separately
- `bgfx_init(&init)` returns bool

## Platform Data (Linux)
- `BGFX_NATIVE_WINDOW_HANDLE_TYPE_DEFAULT` = X11 on Linux
- `BGFX_NATIVE_WINDOW_HANDLE_TYPE_WAYLAND` must be set explicitly when using Wayland surface handles
- X11 Window is a number (not pointer): get via `SDL_GetNumberProperty`, cast to `void*` via `uintptr_t`

## View State
- View state (rect, clear, transform) is per-frame -- must be set every frame before `bgfx_frame()`
- `bgfx_touch(viewId)` submits empty draw call so view clear actually executes
- Without `bgfx_touch`, an untouched view gets no clear (backbuffer garbage)

## Reset / Resize
- `bgfx_reset(w, h, flags, format)` -- pass `BGFX_TEXTURE_FORMAT_COUNT` for default format
- `BGFX_RESET_VSYNC` for vsync, consider `BGFX_RESET_HIDPI` for HiDPI displays
- For SDL3 HiDPI: use ONLY `SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED` (pixel coords), NOT `SDL_EVENT_WINDOW_RESIZED` (logical coords)

## Shutdown
- Order: `bgfx_shutdown()` BEFORE `SDL_DestroyWindow()` BEFORE `SDL_Quit()`
- bgfx holds native window handle refs -- destroying window first = dangling pointer crash on Vulkan/Metal

## Minimize
- Call `bgfx_frame(false)` even when minimized to keep bgfx render thread alive
- `SDL_Delay(16)` prevents busy-spin (~60 wake-ups/sec while minimized)
