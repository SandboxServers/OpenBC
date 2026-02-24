#include "client_backend.h"

#include <bgfx/c99/bgfx.h>
#include <bgfx/defines.h>
#include <SDL3/SDL.h>

#include <limits.h>
#include <stdint.h>
#include <stdio.h>

static SDL_Window *g_window = NULL;
static bool g_running = false;
static bool g_minimized = false;
static uint16_t g_width = 1280;
static uint16_t g_height = 720;

static bgfx_renderer_type_t choose_renderer_type(void)
{
#ifdef _WIN32
    return BGFX_RENDERER_TYPE_DIRECT3D11;
#elif defined(__APPLE__)
    return BGFX_RENDERER_TYPE_METAL;
#else
    /* Prefer Vulkan on Linux; bgfx fallback can pick OpenGL if needed. */
    return BGFX_RENDERER_TYPE_VULKAN;
#endif
}

static bool set_platform_data(SDL_Window *window)
{
    SDL_PropertiesID props = SDL_GetWindowProperties(window);
    bgfx_platform_data_t pd;

    if (props == 0) {
        return false;
    }

    pd.ndt = NULL;
    pd.nwh = NULL;
    pd.context = NULL;
    pd.backBuffer = NULL;
    pd.backBufferDS = NULL;
    pd.type = BGFX_NATIVE_WINDOW_HANDLE_TYPE_DEFAULT;

#ifdef _WIN32
    pd.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
#elif defined(__APPLE__)
    pd.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_COCOA_WINDOW_POINTER, NULL);
#else
    pd.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_DISPLAY_POINTER, NULL);
    pd.nwh = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_WAYLAND_SURFACE_POINTER, NULL);
    if (pd.nwh) {
        pd.type = BGFX_NATIVE_WINDOW_HANDLE_TYPE_WAYLAND;
    }

    if (!pd.nwh) {
        pd.ndt = NULL; /* Clear stale Wayland display before X11 fallback. */
        uint64_t x11_window = (uint64_t)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_X11_WINDOW_NUMBER, 0);
        pd.ndt = SDL_GetPointerProperty(props, SDL_PROP_WINDOW_X11_DISPLAY_POINTER, NULL);
        pd.nwh = (void *)(uintptr_t)x11_window;
    }
#endif

    if (!pd.nwh) {
        return false;
    }

    bgfx_set_platform_data(&pd);
    return true;
}

static void apply_view(uint32_t clear_rgba)
{
    bgfx_set_view_rect(0, 0, 0, g_width, g_height);
    bgfx_set_view_clear(0, BGFX_CLEAR_COLOR | BGFX_CLEAR_DEPTH, clear_rgba, 1.0f, 0);
}

bool bc_client_backend_init(const bc_client_config_t *cfg)
{
    bgfx_init_t init;

    if (!cfg || !cfg->title || cfg->width == 0 || cfg->height == 0) {
        return false;
    }
    if (cfg->width > INT_MAX || cfg->height > INT_MAX) {
        fprintf(stderr, "Requested window size exceeds SDL int range: %lu x %lu\n",
                (unsigned long)cfg->width, (unsigned long)cfg->height);
        return false;
    }

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        fprintf(stderr, "SDL_Init failed: %s\n", SDL_GetError());
        return false;
    }

    g_window = SDL_CreateWindow(cfg->title,
                                (int)cfg->width,
                                (int)cfg->height,
                                SDL_WINDOW_RESIZABLE | SDL_WINDOW_HIGH_PIXEL_DENSITY);
    if (!g_window) {
        fprintf(stderr, "SDL_CreateWindow failed: %s\n", SDL_GetError());
        SDL_Quit();
        return false;
    }

    int win_w = 0;
    int win_h = 0;

    if (!SDL_GetWindowSizeInPixels(g_window, &win_w, &win_h)) {
        fprintf(stderr, "SDL_GetWindowSizeInPixels failed: %s\n", SDL_GetError());
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        SDL_Quit();
        return false;
    }
    if (win_w <= 0 || win_h <= 0 || win_w > UINT16_MAX || win_h > UINT16_MAX) {
        fprintf(stderr, "SDL_GetWindowSizeInPixels returned out-of-range size: %d x %d\n", win_w, win_h);
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        SDL_Quit();
        return false;
    }
    g_width = (uint16_t)win_w;
    g_height = (uint16_t)win_h;

    if (!set_platform_data(g_window)) {
        fprintf(stderr, "Failed to acquire native window handle for bgfx\n");
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        SDL_Quit();
        return false;
    }

    bgfx_init_ctor(&init);
    init.type = choose_renderer_type();
    init.fallback = true;
    init.resolution.width = g_width;
    init.resolution.height = g_height;
    init.resolution.reset = BGFX_RESET_VSYNC;

    if (!bgfx_init(&init)) {
        fprintf(stderr, "bgfx_init failed\n");
        SDL_DestroyWindow(g_window);
        g_window = NULL;
        SDL_Quit();
        return false;
    }

    apply_view(0x04090fff);
    g_running = true;
    g_minimized = false;
    return true;
}

bool bc_client_backend_frame(uint32_t clear_rgba)
{
    SDL_Event event;

    /* Bootstrap event subset; future passes can add focus/display/exposed hooks. */
    if (!g_running) {
        return false;
    }

    while (SDL_PollEvent(&event)) {
        switch (event.type) {
        case SDL_EVENT_QUIT:
        case SDL_EVENT_WINDOW_CLOSE_REQUESTED:
            g_running = false;
            break;

        case SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED:
            if (event.window.data1 > 0 && event.window.data2 > 0 &&
                event.window.data1 <= UINT16_MAX && event.window.data2 <= UINT16_MAX) {
                g_width = (uint16_t)event.window.data1;
                g_height = (uint16_t)event.window.data2;
                bgfx_reset(g_width, g_height, BGFX_RESET_VSYNC, BGFX_TEXTURE_FORMAT_COUNT);
            }
            break;

        case SDL_EVENT_WINDOW_MINIMIZED:
            g_minimized = true;
            break;

        case SDL_EVENT_WINDOW_RESTORED:
            g_minimized = false;
            break;

        default:
            break;
        }
    }

    if (!g_running) {
        return false;
    }

    if (g_minimized) {
        SDL_Delay(16);
        bgfx_frame(false);
        return true;
    }

    apply_view(clear_rgba);
    bgfx_touch(0);
    bgfx_frame(false);
    return true;
}

void bc_client_backend_shutdown(void)
{
    if (g_running) {
        g_running = false;
    }

    bgfx_shutdown();

    if (g_window) {
        SDL_DestroyWindow(g_window);
        g_window = NULL;
    }

    SDL_Quit();
}

const char *bc_client_backend_name(void)
{
    return "sdl3-bgfx";
}
