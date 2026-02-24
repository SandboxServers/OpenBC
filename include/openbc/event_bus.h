#ifndef OPENBC_EVENT_BUS_H
#define OPENBC_EVENT_BUS_H

#include <stdbool.h>

/*
 * Event Bus -- central communication mechanism for the OpenBC plugin engine.
 *
 * Modules subscribe to named events during load. The engine (and other modules)
 * fire events as they occur. Handlers run in priority order (lower number first).
 *
 * See docs/architecture/event-system.md for the full event catalog.
 */

/* Forward declaration -- full definition provided by include/openbc/module_api.h */
struct obc_engine_api;
typedef struct obc_engine_api obc_engine_api_t;

/* Maximum subscribers per event name. */
#define OBC_EVENT_MAX_SUBS    64

/* Maximum distinct event names the bus tracks simultaneously. */
#define OBC_EVENT_MAX_EVENTS  128

/* Maximum length of an event name (including NUL terminator). */
#define OBC_EVENT_NAME_MAX    64

/*
 * Event context passed to every handler.
 *
 * Handlers may:
 *   - Read event_data to react (always safe; cast to the typed event struct)
 *   - Set cancelled = true to stop lower-priority handlers from running
 *   - Set suppress_relay = true to prevent network relay of this event
 *   - Call engine API functions to mutate game state
 */
typedef struct obc_event_ctx {
    const char *event_name;     /* Event identifier (e.g. "ship_killed")        */
    int         sender_slot;    /* Player slot that triggered this; -1 if engine */
    void       *event_data;     /* Typed payload; cast based on event_name       */
    bool        cancelled;      /* Set true to stop further handler dispatch      */
    bool        suppress_relay; /* Set true to prevent network relay              */
} obc_event_ctx_t;

/* Handler function type. api is the same table received at module load. */
typedef void (*obc_event_handler_fn)(const obc_engine_api_t *api,
                                     obc_event_ctx_t        *ctx);

/* --- Lifecycle ------------------------------------------------------------ */

/* Initialise the event bus. Must be called once before any other function. */
void obc_event_bus_init(void);

/* Reset all subscriptions and release internal state. */
void obc_event_bus_shutdown(void);

/* --- Core API ------------------------------------------------------------- */

/*
 * Subscribe fn to event_name at the given priority (0=highest, 255=lowest).
 * Equal-priority handlers fire in registration order.
 * Returns 0 on success, -1 if OBC_EVENT_MAX_EVENTS or OBC_EVENT_MAX_SUBS
 * would be exceeded.
 */
int obc_event_subscribe(const char *event_name, obc_event_handler_fn fn,
                        int priority);

/*
 * Remove a previously registered handler. Safe to call from within a handler
 * (removal is deferred until the current fire completes).
 * No-op if event_name or fn is not found.
 */
void obc_event_unsubscribe(const char *event_name, obc_event_handler_fn fn);

/*
 * Fire event_name. Calls all registered handlers in priority order.
 * Stops early if any handler sets ctx->cancelled = true.
 * api is passed unchanged to every handler. May be NULL in unit tests.
 */
void obc_event_fire(const obc_engine_api_t *api, const char *event_name,
                    int sender_slot, void *data);

#endif /* OPENBC_EVENT_BUS_H */
