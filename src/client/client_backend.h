#ifndef OPENBC_CLIENT_BACKEND_H
#define OPENBC_CLIENT_BACKEND_H

#include <stdbool.h>
#include <stdint.h>

typedef struct bc_client_config_s {
    const char *title;
    uint16_t width;
    uint16_t height;
} bc_client_config_t;

bool bc_client_backend_init(const bc_client_config_t *cfg);
bool bc_client_backend_frame(uint32_t clear_rgba);
void bc_client_backend_shutdown(void);
const char *bc_client_backend_name(void);

#endif /* OPENBC_CLIENT_BACKEND_H */
