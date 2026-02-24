#include "client_backend.h"

#ifdef _WIN32
#  include <windows.h>
#else
#  include <unistd.h>
#endif

static bool g_running = false;

bool bc_client_backend_init(const bc_client_config_t *cfg)
{
    (void)cfg;
    g_running = true;
    return true;
}

bool bc_client_backend_frame(uint32_t clear_rgba)
{
    static unsigned frame_count = 0;

    (void)clear_rgba;

    if (!g_running) {
        return false;
    }

#ifdef _WIN32
    Sleep(16);
#else
    usleep(16000);
#endif

    frame_count++;

    /* No graphics backend: run briefly so automation doesn't hang forever. */
    if (frame_count >= 5) {
        g_running = false;
    }

    return g_running;
}

void bc_client_backend_shutdown(void)
{
    g_running = false;
}

const char *bc_client_backend_name(void)
{
    return "noop";
}
