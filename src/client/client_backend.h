#ifndef OPENBC_CLIENT_BACKEND_H
#define OPENBC_CLIENT_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

typedef struct bc_client_config_s {
    const char *title;
    uint32_t width;
    uint32_t height;
} bc_client_config_t;

/*
 * Minimal bootstrap interface for B2 window/render bring-up.
 * Future expansion is expected for input dispatch, window-state queries,
 * frame timing, and resize callbacks.
 */
bool bc_client_backend_init(const bc_client_config_t *cfg);
bool bc_client_backend_frame(uint32_t clear_rgba);
void bc_client_backend_shutdown(void);
const char *bc_client_backend_name(void);

#endif /* OPENBC_CLIENT_BACKEND_H */
