#include "openbc/event_bus.h"

#include <string.h>

/*
 * Internal storage: one entry per distinct event name, with a sorted list
 * of subscribers (sorted ascending by priority so lower numbers fire first).
 *
 * All state is in static globals -- the event bus is a singleton, matching
 * the single-server-instance model of OpenBC.
 */

typedef struct {
    obc_event_handler_fn fn;
    int                  priority;
} obc_event_sub_t;

typedef struct {
    char            name[OBC_EVENT_NAME_MAX];
    obc_event_sub_t subs[OBC_EVENT_MAX_SUBS];
    int             sub_count;

    /* Re-entrancy / safe-unsubscribe support.
     * Unsubscriptions requested while fire_depth > 0 are queued here and
     * flushed once the outermost fire() on this event returns. */
    int                  fire_depth;
    obc_event_handler_fn remove_pending[OBC_EVENT_MAX_SUBS];
    int                  remove_count;
} obc_event_entry_t;

static obc_event_entry_t g_events[OBC_EVENT_MAX_EVENTS];
static int               g_event_count = 0;

/* --- Internal helpers ----------------------------------------------------- */

static obc_event_entry_t *find_entry(const char *name)
{
    for (int i = 0; i < g_event_count; i++) {
        if (strcmp(g_events[i].name, name) == 0)
            return &g_events[i];
    }
    return NULL;
}

static obc_event_entry_t *find_or_create_entry(const char *name)
{
    obc_event_entry_t *e = find_entry(name);
    if (e) return e;
    if (g_event_count >= OBC_EVENT_MAX_EVENTS) return NULL;

    e = &g_events[g_event_count++];
    memset(e, 0, sizeof(*e));
    strncpy(e->name, name, OBC_EVENT_NAME_MAX - 1);
    e->name[OBC_EVENT_NAME_MAX - 1] = '\0';
    return e;
}

/* Remove one subscriber by function pointer (first occurrence). */
static void remove_sub(obc_event_entry_t *e, obc_event_handler_fn fn)
{
    for (int i = 0; i < e->sub_count; i++) {
        if (e->subs[i].fn == fn) {
            int tail = e->sub_count - i - 1;
            if (tail > 0)
                memmove(&e->subs[i], &e->subs[i + 1],
                        (size_t)tail * sizeof(obc_event_sub_t));
            e->sub_count--;
            return;
        }
    }
}

/* Apply all pending removals accumulated during a fire(). */
static void flush_removals(obc_event_entry_t *e)
{
    for (int r = 0; r < e->remove_count; r++)
        remove_sub(e, e->remove_pending[r]);
    e->remove_count = 0;
}

/* --- Public API ----------------------------------------------------------- */

void obc_event_bus_init(void)
{
    memset(g_events, 0, sizeof(g_events));
    g_event_count = 0;
}

void obc_event_bus_shutdown(void)
{
    g_event_count = 0;
}

int obc_event_subscribe(const char *event_name, obc_event_handler_fn fn,
                        int priority)
{
    if (!event_name || !fn) return -1;
    if (priority < 0)   priority = 0;
    if (priority > 255) priority = 255;

    obc_event_entry_t *e = find_or_create_entry(event_name);
    if (!e || e->sub_count >= OBC_EVENT_MAX_SUBS) return -1;

    /* Find the insertion index: first existing sub with strictly higher
     * priority value (lower priority = runs later). Equal priorities keep
     * registration order by inserting after existing equal entries. */
    int insert = e->sub_count;
    for (int i = 0; i < e->sub_count; i++) {
        if (e->subs[i].priority > priority) {
            insert = i;
            break;
        }
    }

    /* Shift tail right to open a slot. */
    int tail = e->sub_count - insert;
    if (tail > 0)
        memmove(&e->subs[insert + 1], &e->subs[insert],
                (size_t)tail * sizeof(obc_event_sub_t));

    e->subs[insert].fn       = fn;
    e->subs[insert].priority = priority;
    e->sub_count++;
    return 0;
}

void obc_event_unsubscribe(const char *event_name, obc_event_handler_fn fn)
{
    if (!event_name || !fn) return;

    obc_event_entry_t *e = find_entry(event_name);
    if (!e) return;

    if (e->fire_depth > 0) {
        /* Defer removal: we're currently iterating subs for this event. */
        if (e->remove_count < OBC_EVENT_MAX_SUBS)
            e->remove_pending[e->remove_count++] = fn;
        return;
    }

    remove_sub(e, fn);
}

void obc_event_fire(const obc_engine_api_t *api, const char *event_name,
                    int sender_slot, void *data)
{
    if (!event_name) return;

    obc_event_entry_t *e = find_entry(event_name);
    if (!e || e->sub_count == 0) return;

    obc_event_ctx_t ctx;
    ctx.event_name     = event_name;
    ctx.sender_slot    = sender_slot;
    ctx.event_data     = data;
    ctx.cancelled      = false;
    ctx.suppress_relay = false;

    e->fire_depth++;
    for (int i = 0; i < e->sub_count; i++) {
        if (ctx.cancelled) break;
        e->subs[i].fn(api, &ctx);
    }
    e->fire_depth--;

    if (e->fire_depth == 0 && e->remove_count > 0)
        flush_removals(e);
}
