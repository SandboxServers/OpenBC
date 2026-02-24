#include "client_backend.h"

#include <stdio.h>

int main(void)
{
    const bc_client_config_t cfg = {
        "OpenBC Client",
        1280,
        720,
    };
    const uint32_t clear_color = 0x04090fff;

    if (!bc_client_backend_init(&cfg)) {
        fprintf(stderr, "Client startup failed for backend '%s'\n", bc_client_backend_name());
        return 1;
    }

    printf("OpenBC client started with backend: %s\n", bc_client_backend_name());

    while (bc_client_backend_frame(clear_color)) {
        /* Frame work is backend-driven in this bootstrap stage. */
    }

    bc_client_backend_shutdown();
    return 0;
}
