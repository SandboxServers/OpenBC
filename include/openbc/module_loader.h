#ifndef OPENBC_MODULE_LOADER_H
#define OPENBC_MODULE_LOADER_H

#include "openbc/module_api.h"
#include "openbc/config.h"

/*
 * Module Loader -- loads C DLL modules via LoadLibrary/dlopen, calls their
 * obc_module_load entry point with the engine API table, and manages the
 * full lifecycle (load, init, shutdown, unload).
 *
 * Loading order follows the [[modules]] array in server.toml.
 * Shutdown calls modules in reverse order.
 */

/* Maximum number of loaded modules (matches OBC_CFG_MODULES_MAX). */
#define OBC_MODULE_MAX  OBC_CFG_MODULES_MAX

/* Per-module runtime state. */
typedef struct {
    obc_module_t  module;      /* Module handle (name, user_data, shutdown cb) */
    void         *dl_handle;   /* Platform DLL handle (HMODULE / void*) */
    char          dll_path[256]; /* Path used to load the DLL */
} obc_loaded_module_t;

/* Module loader state. */
typedef struct {
    obc_loaded_module_t modules[OBC_MODULE_MAX];
    int                 count;
    obc_engine_api_t    api;   /* Shared engine API table for all modules */
} obc_module_loader_t;

/*
 * Validate a module DLL path for security.
 * Rejects: NULL/empty, path traversal (../), absolute paths, wrong extensions.
 * Returns 0 if valid, -1 if rejected (with LOG_ERROR).
 */
int obc_module_path_validate(const char *path);

/*
 * Populate the engine API table with wrapper functions.
 * cfg is stored for per-module config lookups.
 */
void obc_module_api_build(obc_engine_api_t *api, const obc_server_cfg_t *cfg);

/*
 * Load all DLL modules listed in cfg->modules[].
 * Lua-only entries are skipped with a log message.
 * On failure: partially loaded modules are shut down and unloaded.
 * Returns 0 on success, -1 on failure.
 */
int obc_module_loader_init(obc_module_loader_t *loader,
                           const obc_server_cfg_t *cfg);

/*
 * Shut down and unload all modules in reverse order.
 * Calls each module's shutdown callback (if set), then closes the DLL handle.
 * Safe to call on a zero-initialized or partially initialized loader.
 */
void obc_module_loader_shutdown(obc_module_loader_t *loader);

#endif /* OPENBC_MODULE_LOADER_H */
