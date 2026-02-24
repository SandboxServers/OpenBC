#include "openbc/event_bus.h"

#include <string.h>

#define OBC_EVENT_MAX_FIRE_DEPTH 8

/*
 * Internal storage: one entry per distinct event name, with a sorted list
 * of subscribers (sorted ascending by priority so lower numbers fire first).
 *
 * All state is in static globals -- the event bus is a singleton, matching the
 * single-server-instance model of OpenBC.
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
     * Mutations requested while fire_depth > 0 are queued here and flushed
     * once the outermost fire() on this event returns. */
    int                  fire_depth;

    /* Pending removals (unsubscribe-during-fire). */
    obc_event_handler_fn remove_pending[OBC_EVENT_MAX_SUBS];
    int                  remove_count;

    /* Pending additions (subscribe-during-fire).
     * New handlers added while fire_depth > 0 are queued here and inserted
     * after the current fire cycle completes.  They do NOT run in the same
     * fire() invocation that triggered the subscribe call. */
    obc_event_sub_t add_pending[OBC_EVENT_MAX_SUBS];
    int             add_count;
} obc_event_entry_t;

static obc_event_entry_t g_events[OBC_EVENT_MAX_EVENTS];
static int               g_event_count = 0;

/* This bus is single-threaded. fire_depth and deferred queues make recursive
 * calls safe on the same thread, but concurrent calls from multiple threads
 * are data races and require external serialization. */

/* --- Internal helpers ----------------------------------------------------- */

/* Valid names are non-empty and must fit in OBC_EVENT_NAME_MAX (including NUL). */
static int validate_event_name(const char *name, size_t *out_len)
{
    if (!name) return 0;

    for (size_t len = 0; len < OBC_EVENT_NAME_MAX; len++) {
        if (name[len] == '\0') {
            if (len == 0) return 0;
            if (out_len) *out_len = len;
            return 1;
        }
    }

    return 0;
}

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
    size_t name_len = 0;
    if (!validate_event_name(name, &name_len)) return NULL;

    obc_event_entry_t *e = find_entry(name);
    if (e) return e;
    if (g_event_count >= OBC_EVENT_MAX_EVENTS) return NULL;

    e = &g_events[g_event_count++];
    memset(e, 0, sizeof(*e));
    memcpy(e->name, name, name_len + 1);
    return e;
}

/* Insert one subscriber into e->subs in sorted priority order. */
static int insert_sub(obc_event_entry_t *e, obc_event_handler_fn fn, int priority)
{
    if (e->sub_count >= OBC_EVENT_MAX_SUBS) return -1;

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

/* Apply all pending additions accumulated during a fire(). */
static void flush_additions(obc_event_entry_t *e)
{
    for (int a = 0; a < e->add_count; a++)
        insert_sub(e, e->add_pending[a].fn, e->add_pending[a].priority);
    e->add_count = 0;
}

/* --- Public API ----------------------------------------------------------- */

void obc_event_bus_init(void)
{
    memset(g_events, 0, sizeof(g_events));
    g_event_count = 0;
}

void obc_event_bus_shutdown(void)
{
    for (int i = 0; i < g_event_count; i++) {
        if (g_events[i].fire_depth > 0)
            return;
    }

    memset(g_events, 0, sizeof(g_events));
    g_event_count = 0;
}

int obc_event_subscribe(const char *event_name, obc_event_handler_fn fn,
                        int priority)
{
    if (!validate_event_name(event_name, NULL) || !fn) return -1;
    if (priority < 0)   priority = 0;
    if (priority > 255) priority = 255;

    obc_event_entry_t *e = find_or_create_entry(event_name);
    if (!e) return -1;

    if (e->fire_depth > 0) {
        /* Defer addition: we're currently iterating subs for this event.
         * The new handler will be inserted after the fire cycle completes
         * and will NOT run in the same obc_event_fire() invocation. */
        if (e->sub_count + e->add_count >= OBC_EVENT_MAX_SUBS) return -1;
        e->add_pending[e->add_count].fn       = fn;
        e->add_pending[e->add_count].priority = priority;
        e->add_count++;
        return 0;
    }

    return insert_sub(e, fn, priority);
}

void obc_event_unsubscribe(const char *event_name, obc_event_handler_fn fn)
{
    if (!validate_event_name(event_name, NULL) || !fn) return;

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

obc_event_result_t obc_event_fire(const obc_engine_api_t *api,
                                   const char             *event_name,
                                   int                     sender_slot,
                                   const void             *data)
{
    obc_event_result_t result = { false, false };
    if (!validate_event_name(event_name, NULL)) return result;

    obc_event_entry_t *e = find_entry(event_name);
    if (!e || e->sub_count == 0) return result;

    if (e->fire_depth >= OBC_EVENT_MAX_FIRE_DEPTH)
        return result;

    obc_event_ctx_t ctx;
    ctx.event_name     = event_name;
    ctx.sender_slot    = sender_slot;
    ctx.event_data     = data;
    ctx.cancelled      = false;
    ctx.suppress_relay = false;

    bool cancelled_latched = false;
    bool suppress_latched  = false;

    e->fire_depth++;
    for (int i = 0; i < e->sub_count; i++) {
        if (cancelled_latched)
            break;

        ctx.cancelled      = cancelled_latched;
        ctx.suppress_relay = suppress_latched;
        e->subs[i].fn(api, &ctx);

        if (ctx.cancelled)
            cancelled_latched = true;
        if (ctx.suppress_relay)
            suppress_latched = true;
    }
    e->fire_depth--;

    /* Propagate handler decisions to the caller. */
    result.cancelled      = cancelled_latched;
    result.suppress_relay = suppress_latched;

    if (e->fire_depth == 0) {
        /* Removals intentionally flush before additions. Unsubscribe-during-fire
         * only targets handlers active in the current cycle; handlers added
         * during this cycle become active starting with the next cycle. */
        if (e->remove_count > 0)
            flush_removals(e);
        if (e->add_count > 0)
            flush_additions(e);
    }

    return result;
}
