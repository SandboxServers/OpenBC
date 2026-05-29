/*
 * test_friendly_fire.c — Regression tests for Issue #203
 *
 * Wire-protocol trace analysis of a stock dedicated server session shows the
 * server maintains a cumulative friendly-fire (same-team) damage accumulator
 * with a warning threshold (typically 100 points) and an optional tolerance
 * ceiling that ends the game. The three configurable modes are:
 *
 *   PERMISSIVE — no tracking, no warnings, no game-over.
 *   WARNING    — track + one-shot warning at warning_points; never end game.
 *   STRICT     — track + warning + game-over once tolerance is exceeded.
 *
 * These tests exercise the pure accumulation/decision step bc_ff_step().
 */

#include "test_util.h"
#include "openbc/combat.h"
#include <string.h>

static bc_friendly_fire_t make_ff(bc_ff_mode_t mode, f32 tol, f32 warn,
                                  bool game_over)
{
    bc_friendly_fire_t ff;
    memset(&ff, 0, sizeof(ff));
    ff.mode = mode;
    ff.tolerance = tol;
    ff.warning_points = warn;
    ff.current = 0.0f;
    ff.game_over_on_threshold = game_over;
    ff.warned = false;
    return ff;
}

/*
 * #203-A: PERMISSIVE mode is a no-op — no accumulation, no warnings.
 */
TEST(permissive_is_noop)
{
    bc_friendly_fire_t ff = make_ff(BC_FF_MODE_PERMISSIVE, 1000.0f, 100.0f, false);
    bc_ff_outcome_t out = bc_ff_step(&ff, 500.0f);
    ASSERT(!out.warned);
    ASSERT(!out.game_over);
    ASSERT(ff.current == 0.0f);
    ASSERT(!ff.warned);
}

/*
 * #203-B: WARNING mode accumulates and fires a one-shot warning at the
 * threshold, but never reports game-over.
 */
TEST(warning_one_shot)
{
    bc_friendly_fire_t ff = make_ff(BC_FF_MODE_WARNING, 1000.0f, 100.0f, false);

    /* Below threshold: accumulate, no warning. */
    bc_ff_outcome_t o1 = bc_ff_step(&ff, 40.0f);
    ASSERT(!o1.warned);
    ASSERT(ff.current == 40.0f);

    /* Crossing the threshold fires the warning exactly once. */
    bc_ff_outcome_t o2 = bc_ff_step(&ff, 70.0f); /* current = 110 >= 100 */
    ASSERT(o2.warned);
    ASSERT(!o2.game_over);
    ASSERT(ff.warned);

    /* Further FF damage does NOT re-fire the warning (one-shot latch). */
    bc_ff_outcome_t o3 = bc_ff_step(&ff, 2000.0f); /* far past tolerance too */
    ASSERT(!o3.warned);
    /* WARNING mode never ends the game even when tolerance is exceeded. */
    ASSERT(!o3.game_over);
}

/*
 * #203-C: STRICT mode ends the game once tolerance is exceeded.
 */
TEST(strict_ends_game)
{
    bc_friendly_fire_t ff = make_ff(BC_FF_MODE_STRICT, 1000.0f, 100.0f, true);

    /* Warning fires at warning_points but no game-over yet. */
    bc_ff_outcome_t o1 = bc_ff_step(&ff, 150.0f);
    ASSERT(o1.warned);
    ASSERT(!o1.game_over);

    /* Below tolerance still: no game-over. */
    bc_ff_outcome_t o2 = bc_ff_step(&ff, 500.0f); /* current = 650 */
    ASSERT(!o2.game_over);

    /* Exceed tolerance: game-over reported. */
    bc_ff_outcome_t o3 = bc_ff_step(&ff, 500.0f); /* current = 1150 >= 1000 */
    ASSERT(o3.game_over);
}

/*
 * #203-D: Non-positive / invalid damage is ignored (no accumulation).
 */
TEST(ignores_non_positive)
{
    bc_friendly_fire_t ff = make_ff(BC_FF_MODE_WARNING, 1000.0f, 100.0f, false);
    bc_ff_outcome_t o1 = bc_ff_step(&ff, 0.0f);
    ASSERT(!o1.warned && ff.current == 0.0f);
    bc_ff_outcome_t o2 = bc_ff_step(&ff, -50.0f);
    ASSERT(!o2.warned && ff.current == 0.0f);
}

/*
 * #203-E: A zero warning_points disables the warning emit (but accumulation
 * still happens for the tolerance check in strict mode).
 */
TEST(zero_warning_points_no_warn)
{
    bc_friendly_fire_t ff = make_ff(BC_FF_MODE_STRICT, 200.0f, 0.0f, true);
    bc_ff_outcome_t o1 = bc_ff_step(&ff, 150.0f);
    ASSERT(!o1.warned);
    ASSERT(!o1.game_over);
    bc_ff_outcome_t o2 = bc_ff_step(&ff, 100.0f); /* current = 250 >= 200 */
    ASSERT(!o2.warned);
    ASSERT(o2.game_over);
}

TEST_MAIN_BEGIN()
    RUN(permissive_is_noop);
    RUN(warning_one_shot);
    RUN(strict_ends_game);
    RUN(ignores_non_positive);
    RUN(zero_warning_points_no_warn);
TEST_MAIN_END()
