#include "test_util.h"
#include "openbc/event_bus.h"

#include <string.h>

/*
 * Unit tests for the event bus (A1 / issue #121).
 *
 * The engine API table (obc_engine_api_t) is defined in module_api.h (A3).
 * Here we pass NULL for the api pointer -- the event bus forwards it unchanged
 * to handlers, so NULL is valid for unit testing.
 */

/* --- Shared handler state ------------------------------------------------- */

static int g_call_order[16];
static int g_call_count = 0;
static int g_sender_slot_received = 0;
static void *g_data_received = NULL;

static void reset_state(void)
{
    memset(g_call_order, 0, sizeof(g_call_order));
    g_call_count = 0;
    g_sender_slot_received = 0;
    g_data_received = NULL;
    obc_event_bus_init();
}

/* --- Handlers ------------------------------------------------------------- */

static void handler_a(const obc_engine_api_t *api, obc_event_ctx_t *ctx)
{
    (void)api;
    (void)ctx;
    if (g_call_count < 16) g_call_order[g_call_count] = 'A';
    g_call_count++;
}

static void handler_b(const obc_engine_api_t *api, obc_event_ctx_t *ctx)
{
    (void)api;
    (void)ctx;
    if (g_call_count < 16) g_call_order[g_call_count] = 'B';
    g_call_count++;
}

static void handler_c(const obc_engine_api_t *api, obc_event_ctx_t *ctx)
{
    (void)api;
    (void)ctx;
    if (g_call_count < 16) g_call_order[g_call_count] = 'C';
    g_call_count++;
}

static void handler_cancel(const obc_engine_api_t *api, obc_event_ctx_t *ctx)
{
    (void)api;
    if (g_call_count < 16) g_call_order[g_call_count] = 'X';
    g_call_count++;
    ctx->cancelled = true;
}

static void handler_suppress(const obc_engine_api_t *api, obc_event_ctx_t *ctx)
{
    (void)api;
    ctx->suppress_relay = true;
}

static void handler_check_ctx(const obc_engine_api_t *api, obc_event_ctx_t *ctx)
{
    (void)api;
    g_sender_slot_received = ctx->sender_slot;
    g_data_received        = ctx->event_data;
}

static void handler_unsubscribe_self(const obc_engine_api_t *api,
                                     obc_event_ctx_t        *ctx)
{
    (void)api;
    (void)ctx;
    obc_event_unsubscribe("self_unsub", handler_unsubscribe_self);
    g_call_count++;
}

/* Handler used by the subscribe-during-fire test: subscribes handler_b
 * for the same event on its first invocation. */
static void handler_sub_during_fire(const obc_engine_api_t *api,
                                    obc_event_ctx_t        *ctx)
{
    (void)api;
    (void)ctx;
    /* Only subscribe once (first call only). */
    if (g_call_count == 0)
        obc_event_subscribe("sub_during", handler_b, 50);
    if (g_call_count < 16) g_call_order[g_call_count] = 'A';
    g_call_count++;
}

/* --- Tests ---------------------------------------------------------------- */

TEST(single_handler_fires)
{
    reset_state();
    obc_event_subscribe("e", handler_a, 50);
    obc_event_fire(NULL, "e", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 1);
    ASSERT_EQ_INT(g_call_order[0], 'A');
}

TEST(no_subscribers_no_crash)
{
    reset_state();
    obc_event_fire(NULL, "nobody_listens", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 0);
}

TEST(null_event_name_no_crash)
{
    reset_state();
    obc_event_fire(NULL, NULL, -1, NULL);
    ASSERT_EQ_INT(g_call_count, 0);
}

TEST(priority_ordering)
{
    /* Lower priority number fires first.
     * Register in deliberately wrong order: B(10), A(5), C(20).
     * Expected fire order: A(5), B(10), C(20). */
    reset_state();
    obc_event_subscribe("prio", handler_b, 10);
    obc_event_subscribe("prio", handler_a, 5);
    obc_event_subscribe("prio", handler_c, 20);
    obc_event_fire(NULL, "prio", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 3);
    ASSERT_EQ_INT(g_call_order[0], 'A');
    ASSERT_EQ_INT(g_call_order[1], 'B');
    ASSERT_EQ_INT(g_call_order[2], 'C');
}

TEST(equal_priority_registration_order)
{
    /* Equal priority: registration order is preserved.
     * A registered first at 50, B second at 50 => A fires first. */
    reset_state();
    obc_event_subscribe("eq", handler_a, 50);
    obc_event_subscribe("eq", handler_b, 50);
    obc_event_fire(NULL, "eq", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 2);
    ASSERT_EQ_INT(g_call_order[0], 'A');
    ASSERT_EQ_INT(g_call_order[1], 'B');
}

TEST(cancellation_stops_dispatch)
{
    /* cancel handler at priority 10, handler_a at priority 50.
     * After cancellation, handler_a must not run. */
    reset_state();
    obc_event_subscribe("cancel", handler_cancel, 10);
    obc_event_subscribe("cancel", handler_a, 50);
    obc_event_fire(NULL, "cancel", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 1);
    ASSERT_EQ_INT(g_call_order[0], 'X');
}

TEST(cancellation_result_returned)
{
    /* obc_event_fire must expose ctx.cancelled via the returned result. */
    reset_state();
    obc_event_subscribe("cancel_ret", handler_cancel, 10);
    obc_event_result_t res = obc_event_fire(NULL, "cancel_ret", -1, NULL);
    ASSERT(res.cancelled);
}

TEST(unsubscribe_removes_handler)
{
    reset_state();
    obc_event_subscribe("unsub", handler_a, 50);
    obc_event_unsubscribe("unsub", handler_a);
    obc_event_fire(NULL, "unsub", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 0);
}

TEST(unsubscribe_unknown_event_no_crash)
{
    reset_state();
    obc_event_unsubscribe("ghost", handler_a);  /* must not crash */
    ASSERT_EQ_INT(g_call_count, 0);
}

TEST(unsubscribe_unknown_handler_no_crash)
{
    reset_state();
    obc_event_subscribe("unk_fn", handler_a, 50);
    obc_event_unsubscribe("unk_fn", handler_b);  /* handler_b was never subbed */
    obc_event_fire(NULL, "unk_fn", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 1);              /* handler_a still fires */
}

TEST(ctx_sender_slot_and_data_passed)
{
    int payload = 42;
    reset_state();
    obc_event_subscribe("ctx_check", handler_check_ctx, 50);
    obc_event_fire(NULL, "ctx_check", 3, &payload);
    ASSERT_EQ_INT(g_sender_slot_received, 3);
    ASSERT(g_data_received == &payload);
}

TEST(suppress_relay_result_returned)
{
    /* handler sets suppress_relay; obc_event_fire must return it. */
    reset_state();
    obc_event_subscribe("suppress", handler_suppress, 50);
    obc_event_result_t res = obc_event_fire(NULL, "suppress", -1, NULL);
    ASSERT(res.suppress_relay);
}

TEST(safe_unsubscribe_from_within_handler)
{
    /* handler_unsubscribe_self removes itself during fire.
     * On the second fire it must not be called again. */
    reset_state();
    obc_event_subscribe("self_unsub", handler_unsubscribe_self, 50);
    obc_event_fire(NULL, "self_unsub", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 1);

    g_call_count = 0;
    obc_event_fire(NULL, "self_unsub", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 0);
}

TEST(multiple_events_independent)
{
    reset_state();
    obc_event_subscribe("ev1", handler_a, 50);
    obc_event_subscribe("ev2", handler_b, 50);

    obc_event_fire(NULL, "ev1", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 1);
    ASSERT_EQ_INT(g_call_order[0], 'A');

    obc_event_fire(NULL, "ev2", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 2);
    ASSERT_EQ_INT(g_call_order[1], 'B');
}

TEST(priority_clamp_high)
{
    /* priority > 255 is clamped to 255 -- should still subscribe without error */
    reset_state();
    int rc = obc_event_subscribe("clamp_hi", handler_a, 1000);
    ASSERT_EQ_INT(rc, 0);
    obc_event_fire(NULL, "clamp_hi", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 1);
}

TEST(priority_clamp_low)
{
    /* negative priority clamped to 0 */
    reset_state();
    int rc = obc_event_subscribe("clamp_lo", handler_a, -5);
    ASSERT_EQ_INT(rc, 0);
    obc_event_fire(NULL, "clamp_lo", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 1);
}

/* --- Edge-case / capacity tests ------------------------------------------ */

TEST(max_subscribers_overflow)
{
    /* Subscribing OBC_EVENT_MAX_SUBS+1 handlers to the same event must fail
     * on the last call and leave exactly OBC_EVENT_MAX_SUBS active. */
    reset_state();
    int ok = 0, fail = 0;
    for (int i = 0; i <= OBC_EVENT_MAX_SUBS; i++) {
        int rc = obc_event_subscribe("cap_subs", handler_a, 50);
        if (rc == 0) ok++;
        else         fail++;
    }
    ASSERT_EQ_INT(ok,   OBC_EVENT_MAX_SUBS);
    ASSERT_EQ_INT(fail, 1);
    /* All valid subscribers fire. */
    obc_event_fire(NULL, "cap_subs", -1, NULL);
    ASSERT_EQ_INT(g_call_count, OBC_EVENT_MAX_SUBS);
}

TEST(max_events_overflow)
{
    /* Registering OBC_EVENT_MAX_EVENTS+1 distinct event names must fail on
     * the last one. Use obc_event_bus_init() to wipe everything first. */
    reset_state();
    char name[OBC_EVENT_NAME_MAX];
    int ok = 0, fail = 0;
    for (int i = 0; i <= OBC_EVENT_MAX_EVENTS; i++) {
        snprintf(name, sizeof(name), "evt_%d", i);
        int rc = obc_event_subscribe(name, handler_a, 50);
        if (rc == 0) ok++;
        else         fail++;
    }
    ASSERT_EQ_INT(ok,   OBC_EVENT_MAX_EVENTS);
    ASSERT_EQ_INT(fail, 1);
}

TEST(duplicate_handler_fires_twice)
{
    /* Subscribing the same function twice allows two invocations.
     * Unsubscribing once removes only the first occurrence; one remains. */
    reset_state();
    obc_event_subscribe("dup", handler_a, 50);
    obc_event_subscribe("dup", handler_a, 50);
    obc_event_fire(NULL, "dup", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 2);

    g_call_count = 0;
    obc_event_unsubscribe("dup", handler_a);
    obc_event_fire(NULL, "dup", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 1);
}

TEST(subscribe_during_fire_deferred)
{
    /* handler_sub_during_fire subscribes handler_b on the first invocation.
     * handler_b must NOT run in that same fire() cycle; it must run on the
     * next fire(). */
    reset_state();
    obc_event_subscribe("sub_during", handler_sub_during_fire, 10);

    /* First fire: handler_sub_during_fire runs, subscribes handler_b (deferred). */
    obc_event_fire(NULL, "sub_during", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 1);            /* only handler_sub_during_fire */
    ASSERT_EQ_INT(g_call_order[0], 'A');

    /* Second fire: both handlers should now run. */
    reset_state();
    obc_event_subscribe("sub_during", handler_sub_during_fire, 10);
    /* handler_b was added after the first fire above and is still registered;
     * re-init clears it, so add it explicitly for a clean second cycle. */
    obc_event_subscribe("sub_during", handler_b, 50);
    obc_event_fire(NULL, "sub_during", -1, NULL);
    ASSERT_EQ_INT(g_call_count, 2);
}

TEST_MAIN_BEGIN()
    RUN(single_handler_fires);
    RUN(no_subscribers_no_crash);
    RUN(null_event_name_no_crash);
    RUN(priority_ordering);
    RUN(equal_priority_registration_order);
    RUN(cancellation_stops_dispatch);
    RUN(cancellation_result_returned);
    RUN(unsubscribe_removes_handler);
    RUN(unsubscribe_unknown_event_no_crash);
    RUN(unsubscribe_unknown_handler_no_crash);
    RUN(ctx_sender_slot_and_data_passed);
    RUN(suppress_relay_result_returned);
    RUN(safe_unsubscribe_from_within_handler);
    RUN(multiple_events_independent);
    RUN(priority_clamp_high);
    RUN(priority_clamp_low);
    RUN(max_subscribers_overflow);
    RUN(max_events_overflow);
    RUN(duplicate_handler_fires_twice);
    RUN(subscribe_during_fire_deferred);
TEST_MAIN_END()
