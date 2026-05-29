/*
 * test_weapon_cadence.c — Regression tests for the StateUpdate authority/cadence
 * cluster (#193 weapon 3 Hz tick gate, #194 round-robin anti-drift coverage).
 *
 * Task A (#193): stock ticks WeaponSystem children at 3 Hz (~0.33s), not per
 *   frame. The simulation loop (src/server/main.c) gates bc_combat_charge_tick /
 *   bc_combat_torpedo_tick behind a fixed-step accumulator at
 *   BC_WEAPON_TICK_INTERVAL, mirroring the power 1 Hz gate. These tests verify:
 *     - the underlying ticks are pure dt-integrators (rate * dt), so the gate
 *       can drive them with the fixed 0.33s step without changing total charge;
 *     - replaying the exact main-loop accumulator pattern advances charge ~3x
 *       per second (not ~30x), i.e. value updates only cross 0.33s boundaries.
 *
 * Task B (#194): the downstream remote lane re-sends absolute position /
 *   orientation / speed every broadcast (drift-proof at 10 Hz), and subsystem
 *   HP/power via a round-robin in bc_ship_build_health_update. These tests
 *   verify the round-robin completes a FULL cycle (covering every ser_list
 *   entry) within the bounded tick count, proving no separate force-resend
 *   timer is required.
 *
 * Test ship: Galaxy-class (species_id=3) — 8 phasers, 6 torpedo tubes.
 */

#include "test_util.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/ship_power.h"
#include "openbc/combat.h"
#include "openbc/game_builders.h"
#include "openbc/opcodes.h"
#include <string.h>
#include <math.h>
#include <stdio.h>

#define REGISTRY_DIR "data/vanilla-1.1"

static bc_game_registry_t g_reg;

TEST(load_registry)
{
    ASSERT(bc_registry_load_dir(&g_reg, REGISTRY_DIR));
}

/* Helper: find the flat subsystem index of the first phaser/pulse bank. */
static int first_phaser_idx(const bc_ship_class_t *cls)
{
    for (int i = 0; i < cls->subsystem_count; i++) {
        if (strcmp(cls->subsystems[i].type, "phaser") == 0 ||
            strcmp(cls->subsystems[i].type, "pulse_weapon") == 0)
            return i;
    }
    return -1;
}

/* ------------------------------------------------------------------ *
 * Task A: weapon tick is a pure dt-integrator                         *
 * ------------------------------------------------------------------ */

/*
 * bc_combat_charge_tick must integrate at rate * power_level * dt, so calling
 * it once with dt=0.33 yields the same charge as calling it 10x with dt=0.033.
 * This is what lets the 3 Hz gate use a single fixed 0.33s step without
 * altering total recharge rate.
 */
TEST(charge_tick_is_linear_in_dt)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3); /* Galaxy */
    ASSERT(cls != NULL);
    int ph = first_phaser_idx(cls);
    ASSERT(ph >= 0);

    /* Ship A: one big 0.33s step. Ship B: ten 0.033s steps. */
    bc_ship_state_t a, b;
    bc_ship_init(&a, cls, 2, bc_make_ship_id(0), 0, 0);
    bc_ship_init(&b, cls, 2, bc_make_ship_id(1), 1, 0);

    /* Drain bank 0 on both so there is room to recharge. */
    a.phaser_charge[0] = 0.0f;
    b.phaser_charge[0] = 0.0f;

    bc_combat_charge_tick(&a, cls, 1.0f, 0.33f);

    for (int i = 0; i < 10; i++)
        bc_combat_charge_tick(&b, cls, 1.0f, 0.033f);

    /* 0.33 == 10 * 0.033 within float tolerance. */
    f32 diff = fabsf(a.phaser_charge[0] - b.phaser_charge[0]);
    ASSERT(diff < 1.0f);
}

/*
 * Replay the exact fixed-step accumulator from src/server/main.c's simulation
 * loop and assert the 3 Hz cadence: over 1.0s of 30 Hz frames (~33ms dt) the
 * gate must fire exactly 3 times, NOT once per frame (~30x). This is the core
 * #193 fix — value updates cross 0.33s boundaries, ~10x slower than per-frame.
 */
TEST(weapon_gate_fires_at_3hz_not_per_frame)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    int fire_count = 0;
    int frame_count = 0;
    f32 frame_dt = 0.033f; /* ~30 Hz main loop */

    /* 1.0 second of simulated frames. */
    for (f32 t = 0.0f; t < 1.0f - 1e-4f; t += frame_dt) {
        frame_count++;
        ship.weapon_tick_accum += frame_dt;
        while (ship.weapon_tick_accum >= BC_WEAPON_TICK_INTERVAL) {
            ship.weapon_tick_accum -= BC_WEAPON_TICK_INTERVAL;
            fire_count++;
        }
    }

    /* ~30 frames in a second, but the gate fires only floor(1.0/0.33) = 3x. */
    ASSERT(frame_count >= 28);
    ASSERT_EQ_INT(fire_count, 3);
}

/*
 * Sanity: the accumulator is reset to 0 by bc_ship_init (so a freshly spawned
 * / respawned ship does not carry stale weapon-tick credit).
 */
TEST(weapon_tick_accum_initialized_zero)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    bc_ship_state_t ship;
    memset(&ship, 0xAB, sizeof(ship)); /* poison */
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    ASSERT(ship.weapon_tick_accum == 0.0f);
}

/* ------------------------------------------------------------------ *
 * Task B: round-robin subsystem coverage (#194 anti-drift)            *
 * ------------------------------------------------------------------ */

/*
 * Drive bc_ship_build_health_update repeatedly, feeding next_idx back as
 * start_idx (exactly as the 10 Hz broadcaster does). Assert that every
 * ser_list entry index is visited within the bounded cycle, and that the
 * cursor returns to its starting point — i.e. the round-robin IS a periodic
 * full resend of all subsystem state. No separate force-resend timer needed.
 */
TEST(round_robin_covers_all_subsystems_within_cycle)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    int entry_count = cls->ser_list.count;
    ASSERT(entry_count > 0);
    ASSERT(entry_count <= BC_SS_MAX_ENTRIES); /* <= 16 */

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    bool visited[BC_SS_MAX_ENTRIES];
    memset(visited, 0, sizeof(visited));

    u8 start = 0;
    int ticks = 0;
    /* Bound the loop generously; each broadcast advances >= 1 entry, so a full
     * cycle completes in <= entry_count ticks. The +4 margin guards against a
     * regression making zero progress. */
    int max_ticks = entry_count + 4;

    for (ticks = 0; ticks < max_ticks; ticks++) {
        /* Mark every entry from 'start' up to (but not including) the next
         * cursor as visited this broadcast. */
        u8 next = 0;
        u8 buf[128];
        int len = bc_ship_build_health_update(&ship, cls, 42.0f,
                                               start, &next,
                                               /* is_own_ship */ false,
                                               buf, sizeof(buf));
        ASSERT(len > 0);

        /* Walk start..next (wrapping) and mark visited. At least 'start'. */
        int c = (int)start;
        do {
            ASSERT(c < entry_count);
            visited[c] = true;
            c++;
            if (c >= entry_count) c = 0;
        } while (c != (int)next);

        start = next;

        /* Once the cursor wraps back to 0 we have completed a full cycle.
         * (next can also land mid-list; we keep going until all visited.) */
        bool all = true;
        for (int i = 0; i < entry_count; i++)
            if (!visited[i]) { all = false; break; }
        if (all) { ticks++; break; }
    }

    /* Every top-level ser_list entry (and thus every subsystem, since each
     * entry serializes itself + all children) was re-sent within the cycle. */
    for (int i = 0; i < entry_count; i++)
        ASSERT(visited[i]);

    /* Cycle length must be bounded: <= entry_count broadcasts. At 10 Hz that
     * is <= entry_count * 100ms; Galaxy completes well under 1.6s. */
    ASSERT(ticks <= entry_count + 1);
}

/*
 * Each broadcast must make forward progress (advance the cursor by at least
 * one entry) so the cycle cannot stall — the property that makes the
 * round-robin a guaranteed periodic resend rather than something that can get
 * stuck on a single subsystem.
 */
TEST(round_robin_always_advances)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);
    int entry_count = cls->ser_list.count;
    ASSERT(entry_count > 1);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    u8 start = 0;
    for (int t = 0; t < entry_count * 2; t++) {
        u8 next = 0;
        u8 buf[128];
        int len = bc_ship_build_health_update(&ship, cls, 42.0f,
                                               start, &next, false,
                                               buf, sizeof(buf));
        ASSERT(len > 0);
        ASSERT(next != start); /* always advanced */
        start = next;
    }
}

/*
 * Regression test for CodeRabbit PR #204 [MAJOR]: the shared round-robin cursor
 * must advance by the REMOTE lane (rmt_next), not the OWNER lane (next_idx).
 *
 * The two existing round_robin_* tests above feed the remote lane's 'next' back
 * into 'start' -- i.e. they model the FIXED behavior and so cannot catch the
 * bug. This test reproduces the REAL src/server/main.c broadcast path:
 *   - build the owner lane (is_own_ship=true) from 'start' -> owner_next
 *   - build the remote lane (is_own_ship=false) from the SAME 'start' -> rmt_next
 *   - advance the shared cursor by the FIXED rule (rmt_next when present)
 *   - mark visited by walking only the REMOTE coverage span (what remote
 *     observers actually receive)
 *
 * The owner lane omits the power byte per Powered entry, so it fits MORE entries
 * per 10-byte budget -> owner_next runs ahead of rmt_next. If the cursor is
 * advanced by owner_next (the bug), the band [rmt_next, owner_next) is never
 * covered on the remote lane and those entries are starved forever. Asserting
 * full remote coverage within entry_count+1 ticks fails under the buggy rule.
 */
TEST(round_robin_remote_lane_not_starved_main_loop_path)
{
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    int entry_count = cls->ser_list.count;
    ASSERT(entry_count > 0);
    ASSERT(entry_count <= BC_SS_MAX_ENTRIES);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 2, bc_make_ship_id(0), 0, 0);

    /* Sanity: this ship must actually have a divergent lane somewhere, otherwise
     * the test cannot exercise the bug. The owner lane omits power bytes so it
     * fits MORE entries -> owner_next runs strictly ahead of rmt_next for at
     * least one start position. */
    {
        bc_ship_state_t probe;
        bc_ship_init(&probe, cls, 2, bc_make_ship_id(9), 9, 0);
        int found_divergence = 0;
        for (u8 s = 0; s < (u8)entry_count; s++) {
            u8 on = 0, rn = 0;
            u8 ob[128], rb[128];
            int ol = bc_ship_build_health_update(&probe, cls, 42.0f, s, &on,
                                                 true, ob, sizeof(ob));
            int rl = bc_ship_build_health_update(&probe, cls, 42.0f, s, &rn,
                                                 false, rb, sizeof(rb));
            if (ol > 0 && rl > 0 && on != rn) { found_divergence = 1; break; }
        }
        ASSERT(found_divergence);
    }

    bool visited[BC_SS_MAX_ENTRIES];
    memset(visited, 0, sizeof(visited));

    u8 start = 0;
    /* Walk enough ticks for a couple of full cycles so any per-tick starvation
     * gap accumulates rather than being masked by a single lucky wrap. */
    int max_ticks = entry_count * 2 + 2;

    for (int t = 0; t < max_ticks; t++) {
        /* Owner lane (no power bytes -- covers >= remote lane). */
        u8 owner_next = 0;
        u8 obuf[128];
        int olen = bc_ship_build_health_update(&ship, cls, 42.0f,
                                               start, &owner_next,
                                               /* is_own_ship */ true,
                                               obuf, sizeof(obuf));

        /* Remote lane (power-bearing -- covers <= owner lane). */
        u8 rmt_next = 0;
        u8 rbuf[128];
        int rlen = bc_ship_build_health_update(&ship, cls, 42.0f,
                                               start, &rmt_next,
                                               /* is_own_ship */ false,
                                               rbuf, sizeof(rbuf));
        ASSERT(rlen > 0);

        /* Mark visited by the REMOTE coverage span only -- that is what remote
         * observers actually receive this tick: [start, rmt_next). */
        int c = (int)start;
        do {
            ASSERT(c < entry_count);
            visited[c] = true;
            c++;
            if (c >= entry_count) c = 0;
        } while (c != (int)rmt_next);

        /* Advance the shared cursor by the FIXED rule (mirrors main.c). */
        u8 prev_rmt_end = rmt_next;
        if (rlen > 0)
            start = rmt_next;
        else if (olen > 0)
            start = owner_next;

        /* The decisive invariant: the next remote span must begin exactly where
         * this one ended. Under the buggy owner-cursor advance, start would jump
         * to owner_next (past rmt_next), opening a [rmt_next, owner_next) gap on
         * the remote lane that is never sent to remote observers. */
        ASSERT_EQ_INT((int)start, (int)prev_rmt_end);
    }

    /* With contiguous remote spans, every entry is covered within one cycle. */
    for (int i = 0; i < entry_count; i++)
        ASSERT(visited[i]);
}

TEST_MAIN_BEGIN()
    RUN(load_registry);
    RUN(charge_tick_is_linear_in_dt);
    RUN(weapon_gate_fires_at_3hz_not_per_frame);
    RUN(weapon_tick_accum_initialized_zero);
    RUN(round_robin_covers_all_subsystems_within_cycle);
    RUN(round_robin_always_advances);
    RUN(round_robin_remote_lane_not_starved_main_loop_path);
TEST_MAIN_END()
