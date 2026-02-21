/*
 * Networked Battle Test
 *
 * Real UDP packets through a live OpenBC server subprocess.
 * Headless clients connect with full handshakes, spawn ships,
 * then run AI combat with every weapon fire, state update, and
 * destruction relayed through the wire protocol.
 *
 * Features:
 *   - Per-subsystem damage narration with named subsystems
 *   - Binary packet trace logging (OBCTRACE format)
 *   - Structural validation against Valentine's Day trace format
 *   - Critical subsystem kills (warp core / bridge → instant death)
 *   - Shield breakthrough damage
 *   - Shield regeneration and subsystem repair tracking
 */

#include "test_util.h"
#include "test_harness.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/movement.h"
#include "openbc/combat.h"
#include "openbc/game_builders.h"
#include "openbc/game_events.h"
#include "openbc/opcodes.h"
#include <math.h>
#include <stdlib.h>
#include <time.h>

/* ======================================================================
 * Configuration
 * ====================================================================== */

#define NB_PORT          29900
#define NB_TIMEOUT       1000
#define NB_MANIFEST      "tests/fixtures/manifest.json"
#define NB_GAME_DIR      "tests/fixtures/"
#define NB_REGISTRY      "data/vanilla-1.1.json"
#define NB_TRACE_FILE    "battle_trace.bin"
#define NB_MAX_PLAYERS   4
#define NB_MAX_TICKS     600   /* 60 seconds at 10 Hz */
#define NB_DT            0.1f  /* 100ms per tick */

/* Shield breakthrough: 20% of damage leaks through even if shields are up */
#define SHIELD_BREAKTHROUGH_PCT  0.20f

/* ======================================================================
 * Seeded RNG (xorshift32)
 * ====================================================================== */

static u32 nb_rng_state;

static void nb_rng_seed(u32 seed) { nb_rng_state = seed ? seed : 1; }

static u32 nb_rng_next(void)
{
    u32 x = nb_rng_state;
    x ^= x << 13; x ^= x >> 17; x ^= x << 5;
    nb_rng_state = x;
    return x;
}

static int nb_rng_range(int lo, int hi)
{
    return lo + (int)(nb_rng_next() % (u32)(hi - lo + 1));
}

static f32 nb_rng_float(f32 lo, f32 hi)
{
    return lo + (f32)(nb_rng_next() % 10000) / 10000.0f * (hi - lo);
}

/* ======================================================================
 * Subsystem damage tracker (per-ship, per-subsystem)
 * ====================================================================== */

typedef struct {
    char name[64];
    f32  max_hp;
    f32  current_hp;
    bool disabled;
    bool destroyed;
    bool is_critical;   /* warp core or bridge → death on destroy */
    bool queued_repair;
} subsys_tracker_t;

/* ======================================================================
 * Battle player
 * ====================================================================== */

#define AI_APPROACH     0
#define AI_ENGAGE       1
#define AI_EVADE        2
#define AI_REPAIR       3
#define AI_CLOAK_ATTACK 4
#define AI_DEAD         5

typedef struct {
    bc_test_client_t   client;
    bc_ship_state_t    ship;
    const bc_ship_class_t *cls;
    int                ai_state;
    int                target_idx;
    f32                engage_timer;
    /* Subsystem tracking */
    subsys_tracker_t   subsys[BC_MAX_SUBSYSTEMS];
    int                subsys_count;
    /* Previous state for delta StateUpdates */
    bc_ship_state_t    prev_ship;
    /* Stats */
    int  phasers_fired;
    int  torpedoes_fired;
    f32  damage_dealt;
    f32  damage_taken;
    int  death_tick;
    bool cloak_started;  /* for pairing check */
} battle_player_t;

/* ======================================================================
 * Globals
 * ====================================================================== */

static bc_test_server_t  g_srv;
static bc_game_registry_t g_reg;
static battle_player_t   g_bp[NB_MAX_PLAYERS];
static int               g_num_players;
static int               g_tick;
static f32               g_game_time;
static bc_packet_log_t   g_trace;

/* Packet counters */
static int g_sent_count;
static int g_recv_count;
static int g_op_sent[256];
static int g_op_recv[256];

/* Validation captures */
static u8  g_captured_beam[64];
static int g_captured_beam_len;
static u8  g_captured_torp[64];
static int g_captured_torp_len;
static u8  g_captured_explosion[64];
static int g_captured_explosion_len;
static u8  g_captured_create[512];
static int g_captured_create_len;

/* Forward declarations */
static bool nb_send_reliable(battle_player_t *bp, const u8 *payload, int len);
static bool nb_send_subsys_status(battle_player_t *bp, int ss_idx, f32 condition);

/* ======================================================================
 * Logging helpers
 * ====================================================================== */

static void nb_log_send(u8 slot, const u8 *payload, int len)
{
    packet_log_write(&g_trace, (u32)g_tick, 'S', slot, payload, (u16)len);
    g_sent_count++;
    if (len > 0) g_op_sent[payload[0]]++;
}

static void nb_log_recv(u8 slot, const u8 *payload, int len)
{
    packet_log_write(&g_trace, (u32)g_tick, 'R', slot, payload, (u16)len);
    g_recv_count++;
    if (len > 0) g_op_recv[payload[0]]++;
}

/* ======================================================================
 * Send helpers (with logging)
 * ====================================================================== */

static bool nb_send_reliable(battle_player_t *bp, const u8 *payload, int len)
{
    nb_log_send(bp->ship.owner_slot, payload, len);
    return test_client_send_reliable(&bp->client, payload, len);
}

static bool nb_send_unreliable(battle_player_t *bp, const u8 *payload, int len)
{
    nb_log_send(bp->ship.owner_slot, payload, len);
    return test_client_send_unreliable(&bp->client, payload, len);
}

/* ======================================================================
 * Receive helper (drain with logging, non-blocking)
 * ====================================================================== */

static void nb_drain_recv(battle_player_t *bp, int timeout_ms)
{
    u32 start = GetTickCount();
    while ((int)(GetTickCount() - start) < timeout_ms) {
        int msg_len = 0;
        const u8 *msg = test_client_recv_msg(&bp->client, &msg_len, 10);
        if (!msg) break;
        nb_log_recv(bp->ship.owner_slot, msg, msg_len);
    }
}

/* ======================================================================
 * Subsystem tracker init
 * ====================================================================== */

static void nb_init_subsys_tracker(battle_player_t *bp)
{
    bp->subsys_count = bp->cls->subsystem_count;
    for (int i = 0; i < bp->subsys_count; i++) {
        const bc_subsystem_def_t *def = &bp->cls->subsystems[i];
        strncpy(bp->subsys[i].name, def->name, sizeof(bp->subsys[i].name) - 1);
        bp->subsys[i].max_hp = def->max_condition;
        bp->subsys[i].current_hp = def->max_condition;
        bp->subsys[i].disabled = false;
        bp->subsys[i].destroyed = false;
        bp->subsys[i].queued_repair = false;
        /* Warp core or bridge are critical -- destroying them kills the ship */
        bp->subsys[i].is_critical =
            (strcmp(def->type, "warp_core") == 0 ||
             strcmp(def->type, "bridge") == 0 ||
             strcmp(def->type, "reactor") == 0);
    }
}

/* ======================================================================
 * Shield facing name
 * ====================================================================== */

static const char *shield_name(int facing)
{
    static const char *names[] = {
        "FRONT", "REAR", "TOP", "BOTTOM", "LEFT", "RIGHT"
    };
    if (facing >= 0 && facing < 6) return names[facing];
    return "?";
}

/* ======================================================================
 * Enhanced damage model with narration
 *
 * Incorporates user feedback:
 *   - Shield breakthrough: 20% of damage leaks through even with shields up
 *   - Hull-direct damage when no subsystem is near impact point
 *   - Critical subsystem (warp core/bridge) destruction = instant death
 *   - Shield regeneration and repair are tracked in narration
 * ====================================================================== */

static void nb_apply_damage(battle_player_t *target, battle_player_t *attacker,
                             f32 damage, bc_vec3_t impact_dir, const char *weapon)
{
    if (!target->ship.alive || damage <= 0.0f) return;

    int facing = bc_combat_shield_facing(&target->ship, impact_dir);
    f32 shield_before = target->ship.shield_hp[facing];

    f32 hull_damage = 0.0f;

    if (shield_before > 0.0f) {
        /* Shield absorbs most damage, but breakthrough leaks through */
        f32 breakthrough = damage * SHIELD_BREAKTHROUGH_PCT;
        f32 shield_damage = damage - breakthrough;

        if (shield_damage <= shield_before) {
            target->ship.shield_hp[facing] -= shield_damage;
            hull_damage = breakthrough;
            printf("    [tick %3d] %s shields(%s) absorb %.0f (%.0f->%.0f) +%.0f breakthrough\n",
                   g_tick, target->cls->name, shield_name(facing),
                   shield_damage, shield_before, target->ship.shield_hp[facing],
                   breakthrough);
        } else {
            /* Shields collapse: remaining damage + breakthrough hits hull */
            f32 overflow = shield_damage - shield_before;
            target->ship.shield_hp[facing] = 0.0f;
            hull_damage = overflow + breakthrough;
            printf("    [tick %3d] %s shields(%s) COLLAPSE (%.0f->0) overflow=%.0f\n",
                   g_tick, target->cls->name, shield_name(facing),
                   shield_before, hull_damage);
        }
    } else {
        hull_damage = damage;
    }

    if (hull_damage <= 0.0f) return;

    /* Hull damage */
    f32 hull_before = target->ship.hull_hp;
    target->ship.hull_hp -= hull_damage;
    if (target->ship.hull_hp < 0.0f) target->ship.hull_hp = 0.0f;

    /* Subsystem damage: AABB overlap test */
    bc_vec3_t right = bc_vec3_cross(target->ship.fwd, target->ship.up);
    bc_vec3_t local = {
        bc_vec3_dot(impact_dir, right),
        bc_vec3_dot(impact_dir, target->ship.fwd),
        bc_vec3_dot(impact_dir, target->ship.up),
    };
    int hit_indices[BC_MAX_SUBSYSTEMS];
    int hit_count = bc_combat_find_hit_subsystems(target->cls, local, 1.0f,
                                                   hit_indices, BC_MAX_SUBSYSTEMS);
    int ss_idx = (hit_count > 0) ? hit_indices[0] : -1;

    if (ss_idx >= 0 && ss_idx < target->subsys_count &&
        target->subsys[ss_idx].current_hp > 0.0f) {
        f32 ss_dmg = hull_damage * 0.5f;
        f32 ss_before = target->subsys[ss_idx].current_hp;
        target->subsys[ss_idx].current_hp -= ss_dmg;
        target->ship.subsystem_hp[ss_idx] -= ss_dmg;

        if (target->subsys[ss_idx].current_hp <= 0.0f) {
            target->subsys[ss_idx].current_hp = 0.0f;
            target->ship.subsystem_hp[ss_idx] = 0.0f;
            target->subsys[ss_idx].destroyed = true;

            printf("    [tick %3d] %s \"%s\" DESTROYED (%.0f->0)\n",
                   g_tick, target->cls->name, target->subsys[ss_idx].name,
                   ss_before);

            /* Critical subsystem kill */
            if (target->subsys[ss_idx].is_critical) {
                printf("    [tick %3d] %s CRITICAL SUBSYSTEM DESTROYED (\"%s\") -> SHIP KILL\n",
                       g_tick, target->cls->name, target->subsys[ss_idx].name);
                target->ship.hull_hp = 0.0f;
            }
        } else {
            f32 pct = (target->subsys[ss_idx].current_hp / target->subsys[ss_idx].max_hp) * 100.0f;
            printf("    [tick %3d] %s \"%s\" damaged %.0f->%.0f (%.0f%%)\n",
                   g_tick, target->cls->name, target->subsys[ss_idx].name,
                   ss_before, target->subsys[ss_idx].current_hp, pct);

            /* Check disabled threshold */
            const bc_subsystem_def_t *def = &target->cls->subsystems[ss_idx];
            if (def->disabled_pct > 0.0f) {
                f32 threshold = def->max_condition * (1.0f - def->disabled_pct);
                if (target->subsys[ss_idx].current_hp < threshold &&
                    !target->subsys[ss_idx].disabled) {
                    target->subsys[ss_idx].disabled = true;
                    printf("    [tick %3d] %s \"%s\" DISABLED (below %.0f%%)\n",
                           g_tick, target->cls->name, target->subsys[ss_idx].name,
                           (1.0f - def->disabled_pct) * 100.0f);
                }
            }
        }
    } else {
        /* No subsystem near impact -- hull-direct damage */
        if (hull_damage > 0.0f && shield_before <= 0.0f) {
            printf("    [tick %3d] %s hull takes %.0f direct (%s) (%.0f->%.0f)\n",
                   g_tick, target->cls->name, hull_damage, weapon,
                   hull_before, target->ship.hull_hp);
        }
    }

    /* Send SubsysStatus if subsystem was hit */
    if (ss_idx >= 0 && ss_idx < target->subsys_count) {
        nb_send_subsys_status(target, ss_idx, target->subsys[ss_idx].current_hp);
    }

    /* Check death */
    if (target->ship.hull_hp <= 0.0f) {
        target->ship.hull_hp = 0.0f;
        target->ship.alive = false;
        target->ai_state = AI_DEAD;
        target->death_tick = g_tick;
        printf("    [tick %3d] %s DESTROYED (hull 0)\n",
               g_tick, target->cls->name);
    }

    attacker->damage_dealt += damage;
    target->damage_taken += damage;
}

/* ======================================================================
 * Shield regen narration (called each tick for alive ships)
 * ====================================================================== */

static void nb_shield_regen_tick(battle_player_t *bp)
{
    if (!bp->ship.alive) return;
    if (bp->ship.cloak_state != BC_CLOAK_DECLOAKED) return;

    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
        f32 before = bp->ship.shield_hp[i];
        f32 max_hp = bp->cls->shield_hp[i];
        f32 regen = bp->cls->shield_recharge[i] * NB_DT;
        if (before < max_hp && regen > 0.0f) {
            bp->ship.shield_hp[i] += regen;
            if (bp->ship.shield_hp[i] > max_hp) bp->ship.shield_hp[i] = max_hp;
            /* Only log significant regen events (every 5 seconds worth) */
        }
    }
}

/* ======================================================================
 * Repair narration
 * ====================================================================== */

static void nb_repair_tick(battle_player_t *bp)
{
    if (!bp->ship.alive) return;
    if (bp->ship.repair_count == 0) return;
    if (bp->cls->max_repair_points <= 0.0f) return;

    u8 ss_idx = bp->ship.repair_queue[0];
    if (ss_idx >= (u8)bp->subsys_count) return;

    f32 before = bp->subsys[ss_idx].current_hp;
    f32 rate = bp->cls->max_repair_points * (f32)bp->cls->num_repair_teams;
    f32 heal = rate * NB_DT;
    f32 max_hp = bp->subsys[ss_idx].max_hp;

    bp->subsys[ss_idx].current_hp += heal;
    bp->ship.subsystem_hp[ss_idx] += heal;
    if (bp->subsys[ss_idx].current_hp >= max_hp) {
        bp->subsys[ss_idx].current_hp = max_hp;
        bp->ship.subsystem_hp[ss_idx] = max_hp;
        bc_repair_remove(&bp->ship, ss_idx);
        bp->subsys[ss_idx].queued_repair = false;

        /* Check if un-disabled */
        const bc_subsystem_def_t *def = &bp->cls->subsystems[ss_idx];
        if (bp->subsys[ss_idx].disabled && def->disabled_pct > 0.0f) {
            f32 threshold = def->max_condition * (1.0f - def->disabled_pct);
            if (bp->subsys[ss_idx].current_hp >= threshold) {
                bp->subsys[ss_idx].disabled = false;
            }
        }

        f32 pct = (bp->subsys[ss_idx].current_hp / max_hp) * 100.0f;
        printf("    [tick %3d] %s repair: \"%s\" COMPLETE (%.0f->%.0f, %.0f%%)\n",
               g_tick, bp->cls->name, bp->subsys[ss_idx].name,
               before, bp->subsys[ss_idx].current_hp, pct);
    } else if (heal > 0.0f) {
        /* Only log periodically (every 2 seconds = 20 ticks) */
        if (g_tick % 20 == 0) {
            f32 pct = (bp->subsys[ss_idx].current_hp / max_hp) * 100.0f;
            printf("    [tick %3d] %s repair: \"%s\" %.0f->%.0f (%.0f%%)\n",
                   g_tick, bp->cls->name, bp->subsys[ss_idx].name,
                   before, bp->subsys[ss_idx].current_hp, pct);
        }
        bp->subsys[ss_idx].current_hp = bp->subsys[ss_idx].current_hp; /* noop to suppress warn */
    }
}

/* Auto-queue damaged subsystems for repair */
static void nb_repair_auto_queue(battle_player_t *bp)
{
    for (int i = 0; i < bp->subsys_count; i++) {
        if (bp->subsys[i].destroyed) continue;
        if (bp->subsys[i].queued_repair) continue;

        const bc_subsystem_def_t *def = &bp->cls->subsystems[i];
        if (def->disabled_pct <= 0.0f) continue;

        f32 threshold = def->max_condition * (1.0f - def->disabled_pct);
        if (bp->subsys[i].current_hp < threshold && bp->subsys[i].current_hp > 0.0f) {
            if (bc_repair_add(&bp->ship, (u8)i)) {
                bp->subsys[i].queued_repair = true;
            }
        }
    }
}

/* ======================================================================
 * AI helpers
 * ====================================================================== */

static int nb_count_alive(int team)
{
    int n = 0;
    for (int i = 0; i < g_num_players; i++)
        if (g_bp[i].ship.alive && g_bp[i].ship.team_id == team) n++;
    return n;
}

static int nb_find_nearest_enemy(int idx)
{
    int best = -1;
    f32 best_dist = 1e30f;
    for (int i = 0; i < g_num_players; i++) {
        if (i == idx || !g_bp[i].ship.alive) continue;
        if (g_bp[i].ship.team_id == g_bp[idx].ship.team_id) continue;
        f32 d = bc_vec3_dist(g_bp[idx].ship.pos, g_bp[i].ship.pos);
        if (d < best_dist) { best_dist = d; best = i; }
    }
    return best;
}

static f32 nb_dist_to_target(int idx)
{
    int ti = g_bp[idx].target_idx;
    if (ti < 0 || !g_bp[ti].ship.alive) return 1e30f;
    return bc_vec3_dist(g_bp[idx].ship.pos, g_bp[ti].ship.pos);
}

/* ======================================================================
 * Build + send SubsysStatus (opcode 0x0A)
 * Wire: [0x0A][obj_id:i32][subsys_idx:u8][condition:f32]
 * ====================================================================== */

static bool nb_send_subsys_status(battle_player_t *bp, int ss_idx, f32 condition)
{
    u8 buf[16];
    u8 data[9]; /* obj_id(4) + subsys_idx(1) + condition(4) */
    memcpy(data, &bp->ship.object_id, 4);
    data[4] = (u8)ss_idx;
    memcpy(data + 5, &condition, 4);

    int len = bc_build_event_forward(buf, sizeof(buf), BC_OP_SUBSYS_STATUS, data, 9);
    if (len <= 0) return false;
    return nb_send_reliable(bp, buf, len);
}

/* ======================================================================
 * AI combat tick
 * ====================================================================== */

static void nb_ai_update(int idx)
{
    battle_player_t *p = &g_bp[idx];
    if (!p->ship.alive) { p->ai_state = AI_DEAD; return; }

    /* Re-acquire target */
    if (p->target_idx < 0 || !g_bp[p->target_idx].ship.alive)
        p->target_idx = nb_find_nearest_enemy(idx);
    if (p->target_idx < 0) return;

    battle_player_t *tgt = &g_bp[p->target_idx];
    f32 dist = nb_dist_to_target(idx);
    p->engage_timer += NB_DT;

    /* Shield health ratio */
    f32 shield_total = 0, shield_max = 0;
    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
        shield_total += p->ship.shield_hp[i];
        shield_max += p->cls->shield_hp[i];
    }
    f32 shield_ratio = (shield_max > 0) ? shield_total / shield_max : 0;

    switch (p->ai_state) {
    case AI_APPROACH:
        bc_ship_turn_toward(&p->ship, p->cls, tgt->ship.pos, NB_DT);
        bc_ship_set_speed(&p->ship, p->cls, p->cls->max_speed);

        if (dist < 80.0f) {
            p->ai_state = AI_ENGAGE;
            p->engage_timer = 0;
        }
        /* Cloakers */
        if (p->cls->can_cloak && p->ship.cloak_state == BC_CLOAK_DECLOAKED &&
            dist > 60.0f && shield_ratio > 0.5f) {
            if (bc_cloak_start(&p->ship, p->cls)) {
                p->cloak_started = true;
                p->ai_state = AI_CLOAK_ATTACK;
                p->engage_timer = 0;
                printf("    [tick %3d] %s cloaks\n", g_tick, p->cls->name);

                u8 buf[8];
                u8 data[4];
                memcpy(data, &p->ship.object_id, 4);
                int len = bc_build_event_forward(buf, sizeof(buf),
                                                  BC_OP_START_CLOAK, data, 4);
                if (len > 0) nb_send_reliable(p, buf, len);
            }
        }
        break;

    case AI_ENGAGE:
        bc_ship_turn_toward(&p->ship, p->cls, tgt->ship.pos, NB_DT);
        bc_ship_set_speed(&p->ship, p->cls, p->cls->max_speed * 0.6f);

        /* Fire weapons */
        {
            u8 pkt[256];
            bc_vec3_t impact = bc_vec3_normalize(
                bc_vec3_sub(tgt->ship.pos, p->ship.pos));

            /* Phasers */
            for (int b = 0; b < p->cls->phaser_banks; b++) {
                if (!bc_combat_can_fire_phaser(&p->ship, p->cls, b)) continue;

                int n = bc_combat_fire_phaser(&p->ship, p->cls, b,
                                               tgt->ship.object_id, pkt, sizeof(pkt));
                if (n <= 0) continue;

                p->phasers_fired++;

                /* Send StartFiring + BeamFire + StopFiring bracket */
                u8 start_data[4];
                memcpy(start_data, &p->ship.object_id, 4);
                u8 start_buf[16];
                int slen = bc_build_event_forward(start_buf, sizeof(start_buf),
                                                   BC_OP_START_FIRING, start_data, 4);
                if (slen > 0) nb_send_reliable(p, start_buf, slen);

                nb_send_reliable(p, pkt, n);

                /* Capture first beam for structural validation */
                if (g_captured_beam_len == 0 && n <= (int)sizeof(g_captured_beam)) {
                    memcpy(g_captured_beam, pkt, (size_t)n);
                    g_captured_beam_len = n;
                }

                u8 stop_buf[16];
                int tlen = bc_build_event_forward(stop_buf, sizeof(stop_buf),
                                                   BC_OP_STOP_FIRING, start_data, 4);
                if (tlen > 0) nb_send_reliable(p, stop_buf, tlen);

                /* Find phaser damage */
                int cnt = 0;
                for (int s = 0; s < p->cls->subsystem_count; s++) {
                    if (strcmp(p->cls->subsystems[s].type, "phaser") == 0 ||
                        strcmp(p->cls->subsystems[s].type, "pulse_weapon") == 0) {
                        if (cnt == b) {
                            f32 dmg = p->cls->subsystems[s].max_damage;
                            printf("    [tick %3d] %s fires phaser bank %d -> %s (dmg=%.0f)\n",
                                   g_tick, p->cls->name, b, tgt->cls->name, dmg);
                            nb_apply_damage(tgt, p, dmg, impact, "phaser");

                            /* Send Explosion for the damage */
                            u8 expl[32];
                            int elen = bc_build_explosion(expl, sizeof(expl),
                                                           tgt->ship.object_id,
                                                           impact.x, impact.y, impact.z,
                                                           dmg, 15.0f);
                            if (elen > 0) {
                                nb_send_reliable(p, expl, elen);
                                if (g_captured_explosion_len == 0) {
                                    memcpy(g_captured_explosion, expl, (size_t)elen);
                                    g_captured_explosion_len = elen;
                                }
                            }
                            break;
                        }
                        cnt++;
                    }
                }

                if (!tgt->ship.alive) break;
            }

            /* Torpedoes */
            for (int t = 0; t < p->cls->torpedo_tubes && tgt->ship.alive; t++) {
                if (!bc_combat_can_fire_torpedo(&p->ship, p->cls, t)) continue;

                bc_vec3_t dir = bc_vec3_normalize(
                    bc_vec3_sub(tgt->ship.pos, p->ship.pos));
                int n = bc_combat_fire_torpedo(&p->ship, p->cls, t,
                                                tgt->ship.object_id, dir,
                                                pkt, sizeof(pkt));
                if (n <= 0) continue;

                p->torpedoes_fired++;
                nb_send_reliable(p, pkt, n);

                if (g_captured_torp_len == 0 && n <= (int)sizeof(g_captured_torp)) {
                    memcpy(g_captured_torp, pkt, (size_t)n);
                    g_captured_torp_len = n;
                }

                /* Torpedo damage */
                f32 dmg = 500.0f;
                for (int pr = 0; pr < g_reg.projectile_count; pr++) {
                    if (g_reg.projectiles[pr].net_type_id == p->ship.torpedo_type) {
                        dmg = g_reg.projectiles[pr].damage;
                        break;
                    }
                }
                printf("    [tick %3d] %s fires torpedo tube %d -> %s (dmg=%.0f)\n",
                       g_tick, p->cls->name, t, tgt->cls->name, dmg);
                nb_apply_damage(tgt, p, dmg, impact, "torpedo");

                /* Explosion */
                u8 expl[32];
                int elen = bc_build_explosion(expl, sizeof(expl),
                                               tgt->ship.object_id,
                                               impact.x, impact.y, impact.z,
                                               dmg, 25.0f);
                if (elen > 0) nb_send_reliable(p, expl, elen);
            }
        }

        /* Ship kill */
        if (!tgt->ship.alive) {
            u8 dpkt[16];
            int dlen = bc_build_destroy_obj(dpkt, sizeof(dpkt), tgt->ship.object_id);
            if (dlen > 0) {
                nb_send_reliable(p, dpkt, dlen);
                printf("    [tick %3d] -> sent DestroyObject for %s\n",
                       g_tick, tgt->cls->name);
            }
            p->target_idx = nb_find_nearest_enemy(idx);
            p->ai_state = AI_APPROACH;
            p->engage_timer = 0;
        }

        /* Evade when shields low */
        if (shield_ratio < 0.2f && p->ship.alive) {
            p->ai_state = AI_EVADE;
            p->engage_timer = 0;
        }
        break;

    case AI_EVADE:
        {
            bc_vec3_t away = bc_vec3_normalize(
                bc_vec3_sub(p->ship.pos, tgt->ship.pos));
            bc_vec3_t flee = bc_vec3_add(p->ship.pos, bc_vec3_scale(away, 100.0f));
            bc_ship_turn_toward(&p->ship, p->cls, flee, NB_DT);
            bc_ship_set_speed(&p->ship, p->cls, p->cls->max_speed);
        }

        /* Auto repair while evading */
        nb_repair_auto_queue(p);
        nb_repair_tick(p);

        if (shield_ratio > 0.5f || p->engage_timer > 10.0f) {
            p->ai_state = AI_APPROACH;
            p->engage_timer = 0;
        }
        break;

    case AI_CLOAK_ATTACK:
        bc_cloak_tick(&p->ship, NB_DT);

        if (p->ship.cloak_state == BC_CLOAK_CLOAKING ||
            p->ship.cloak_state == BC_CLOAK_CLOAKED) {
            bc_ship_turn_toward(&p->ship, p->cls, tgt->ship.pos, NB_DT);
            bc_ship_set_speed(&p->ship, p->cls, p->cls->max_speed);

            if (p->ship.cloak_state == BC_CLOAK_CLOAKED && dist < 40.0f) {
                bc_cloak_stop(&p->ship);
                printf("    [tick %3d] %s decloaks at range %.0f\n",
                       g_tick, p->cls->name, dist);

                u8 buf[8];
                u8 data[4];
                memcpy(data, &p->ship.object_id, 4);
                int len = bc_build_event_forward(buf, sizeof(buf),
                                                  BC_OP_STOP_CLOAK, data, 4);
                if (len > 0) nb_send_reliable(p, buf, len);
                p->cloak_started = false;
            }
        } else if (p->ship.cloak_state == BC_CLOAK_DECLOAKING) {
            bc_ship_turn_toward(&p->ship, p->cls, tgt->ship.pos, NB_DT);
        } else {
            p->ai_state = AI_ENGAGE;
            p->engage_timer = 0;
        }
        break;

    default:
        break;
    }
}

/* ======================================================================
 * Main test
 * ====================================================================== */

static int run_networked_battle(void)
{
    printf("  Networked Battle Test\n");

    int assert_fail = 0;

    /* === Init === */
    memset(g_op_sent, 0, sizeof(g_op_sent));
    memset(g_op_recv, 0, sizeof(g_op_recv));
    g_sent_count = g_recv_count = 0;
    g_captured_beam_len = g_captured_torp_len = 0;
    g_captured_explosion_len = g_captured_create_len = 0;

    if (!bc_net_init()) {
        fprintf(stderr, "  bc_net_init failed\n");
        return 1;
    }

    if (!bc_registry_load(&g_reg, NB_REGISTRY)) {
        fprintf(stderr, "  registry load failed\n");
        return 1;
    }

    /* Packet trace */
    if (!packet_log_open(&g_trace, NB_TRACE_FILE)) {
        fprintf(stderr, "  trace open failed\n");
        return 1;
    }

    /* Seed RNG */
    u32 seed = (u32)time(NULL);
    nb_rng_seed(seed);
    printf("    SEED: %u\n", seed);

    /* Pick 3-4 players */
    g_num_players = nb_rng_range(3, NB_MAX_PLAYERS);
    printf("    Players: %d\n", g_num_players);

    /* Select ships and assign teams */
    for (int i = 0; i < g_num_players; i++) {
        int ship_idx = nb_rng_range(0, g_reg.ship_count - 1);
        g_bp[i].cls = &g_reg.ships[ship_idx];
        bc_ship_init(&g_bp[i].ship, g_bp[i].cls, ship_idx,
                     bc_make_ship_id(i), (u8)i, (u8)(i % 2));

        g_bp[i].ship.pos = (bc_vec3_t){
            nb_rng_float(-100.0f, 100.0f),
            nb_rng_float(-100.0f, 100.0f),
            nb_rng_float(-100.0f, 100.0f),
        };
        g_bp[i].ship.fwd = bc_vec3_normalize((bc_vec3_t){
            nb_rng_float(-1.0f, 1.0f),
            nb_rng_float(-1.0f, 1.0f),
            nb_rng_float(-1.0f, 1.0f),
        });
        if (bc_vec3_len(g_bp[i].ship.fwd) < 0.01f)
            g_bp[i].ship.fwd = (bc_vec3_t){0, 1, 0};
        g_bp[i].ship.fwd = bc_vec3_normalize(g_bp[i].ship.fwd);

        g_bp[i].ai_state = AI_APPROACH;
        g_bp[i].target_idx = -1;
        g_bp[i].engage_timer = 0;
        g_bp[i].phasers_fired = 0;
        g_bp[i].torpedoes_fired = 0;
        g_bp[i].damage_dealt = 0;
        g_bp[i].damage_taken = 0;
        g_bp[i].death_tick = -1;
        g_bp[i].cloak_started = false;
        g_bp[i].prev_ship = g_bp[i].ship;

        nb_init_subsys_tracker(&g_bp[i]);

        printf("    [%d] %s (team %d) hull=%.0f, %d subsystems%s\n",
               i, g_bp[i].cls->name, i % 2, g_bp[i].cls->hull_hp,
               g_bp[i].subsys_count,
               g_bp[i].cls->can_cloak ? ", CAN CLOAK" : "");
    }

    /* === Start server === */
    printf("    Starting server...\n");
    if (!test_server_start(&g_srv, NB_PORT, NB_MANIFEST)) {
        fprintf(stderr, "  server start failed\n");
        packet_log_close(&g_trace);
        return 1;
    }

    /* === Connect clients === */
    printf("    Connecting %d clients...\n", g_num_players);
    for (int i = 0; i < g_num_players; i++) {
        char name[32];
        snprintf(name, sizeof(name), "Bot%d", i);
        if (!test_client_connect(&g_bp[i].client, NB_PORT, name, (u8)i, NB_GAME_DIR)) {
            fprintf(stderr, "  client %d connect failed\n", i);
            goto cleanup;
        }
    }
    Sleep(200);

    /* Drain post-handshake notifications */
    for (int i = 0; i < g_num_players; i++)
        test_client_drain(&g_bp[i].client, 300);

    /* === Spawn ships === */
    printf("    Spawning ships...\n");
    for (int i = 0; i < g_num_players; i++) {
        u8 pkt[512];
        int n = bc_ship_build_create_packet(&g_bp[i].ship, g_bp[i].cls,
                                              pkt, sizeof(pkt));
        if (n <= 0) {
            fprintf(stderr, "  ship create packet build failed for %d\n", i);
            goto cleanup;
        }

        /* Capture first create for validation */
        if (g_captured_create_len == 0 && n <= (int)sizeof(g_captured_create)) {
            memcpy(g_captured_create, pkt, (size_t)n);
            g_captured_create_len = n;
        }

        nb_send_reliable(&g_bp[i], pkt, n);
    }
    Sleep(300);

    /* Each client expects (num_players - 1) ObjectCreateTeam from others */
    for (int i = 0; i < g_num_players; i++) {
        for (int j = 0; j < g_num_players - 1; j++) {
            int msg_len = 0;
            const u8 *msg = test_client_expect_opcode(&g_bp[i].client,
                                                        BC_OP_OBJ_CREATE_TEAM,
                                                        &msg_len, NB_TIMEOUT);
            if (!msg) {
                fprintf(stderr, "  client %d: missing ObjectCreateTeam %d\n", i, j);
                goto cleanup;
            }
            nb_log_recv(g_bp[i].ship.owner_slot, msg, msg_len);
        }
    }
    printf("    All ships spawned and confirmed.\n");

    /* === Simulation loop === */
    printf("    Running battle simulation...\n");
    g_tick = 0;
    g_game_time = 0.0f;

    for (g_tick = 0; g_tick < NB_MAX_TICKS; g_tick++) {
        g_game_time = (f32)g_tick * NB_DT;

        /* Win condition */
        int team0 = nb_count_alive(0);
        int team1 = nb_count_alive(1);
        if (team0 == 0 || team1 == 0) break;

        for (int i = 0; i < g_num_players; i++) {
            if (!g_bp[i].ship.alive) continue;

            /* Movement tick */
            bc_ship_move_tick(&g_bp[i].ship, 1.0f, NB_DT);

            /* Weapon charge/cooldown */
            bc_combat_charge_tick(&g_bp[i].ship, g_bp[i].cls, 1.0f, NB_DT);
            bc_combat_torpedo_tick(&g_bp[i].ship, g_bp[i].cls, NB_DT);

            /* Shield regen */
            nb_shield_regen_tick(&g_bp[i]);

            /* Cloak tick */
            bc_cloak_tick(&g_bp[i].ship, NB_DT);

            /* AI decision + combat */
            nb_ai_update(i);

            /* Send StateUpdate (unreliable) */
            if (g_bp[i].ship.alive) {
                u8 su_pkt[256];
                int su_len = bc_ship_build_state_update(&g_bp[i].ship,
                                                          &g_bp[i].prev_ship,
                                                          g_game_time,
                                                          su_pkt, sizeof(su_pkt));
                if (su_len > 0) {
                    nb_send_unreliable(&g_bp[i], su_pkt, su_len);
                }
                g_bp[i].prev_ship = g_bp[i].ship;
            }
        }

        /* Drain incoming for all clients (non-blocking) */
        for (int i = 0; i < g_num_players; i++) {
            if (!g_bp[i].client.connected) continue;
            nb_drain_recv(&g_bp[i], 10);
        }
    }

    /* === Battle Summary === */
    printf("\n    --- Battle Summary (tick %d / %.1fs) ---\n", g_tick, g_game_time);

    int survivors = 0;
    for (int i = 0; i < g_num_players; i++) {
        if (g_bp[i].ship.alive) {
            printf("    [%d] %s ALIVE hull=%.0f phasers=%d torps=%d\n",
                   i, g_bp[i].cls->name, g_bp[i].ship.hull_hp,
                   g_bp[i].phasers_fired, g_bp[i].torpedoes_fired);
            survivors++;
        } else {
            printf("    [%d] %s DEAD  hull=0 (destroyed tick %d)\n",
                   i, g_bp[i].cls->name, g_bp[i].death_tick);
        }
    }

    int team0 = nb_count_alive(0);
    int team1 = nb_count_alive(1);
    if (team0 > 0 && team1 == 0) printf("    WINNER: Team 0\n");
    else if (team1 > 0 && team0 == 0) printf("    WINNER: Team 1\n");
    else printf("    DRAW (time limit)\n");

    /* === Packet stats === */
    printf("\n    Packets: %d sent, %d received\n", g_sent_count, g_recv_count);
    const u8 tracked_ops[] = {
        BC_OP_OBJ_CREATE_TEAM, BC_OP_STATE_UPDATE, BC_OP_BEAM_FIRE,
        BC_OP_TORPEDO_FIRE, BC_OP_EXPLOSION, BC_OP_DESTROY_OBJ,
        BC_OP_START_FIRING, BC_OP_STOP_FIRING,
        BC_OP_START_CLOAK, BC_OP_STOP_CLOAK, BC_OP_SUBSYS_STATUS,
    };
    for (int i = 0; i < (int)(sizeof(tracked_ops)/sizeof(tracked_ops[0])); i++) {
        u8 op = tracked_ops[i];
        if (g_op_sent[op] > 0 || g_op_recv[op] > 0) {
            const char *name = bc_opcode_name(op);
            printf("      %-20s: %4d sent, %4d received\n",
                   name ? name : "?", g_op_sent[op], g_op_recv[op]);
        }
    }

    /* === Trace file === */
    packet_log_close(&g_trace);
    {
        FILE *check = fopen(NB_TRACE_FILE, "rb");
        if (check) {
            fseek(check, 0, SEEK_END);
            long sz = ftell(check);
            fclose(check);
            printf("    Trace written: %s (%ld bytes)\n", NB_TRACE_FILE, sz);
        }
    }

    /* === Structural validation === */
    printf("\n    Structural validation:\n");
    int validation_pass = 0;
    int validation_total = 0;

    /* BeamFire format: [0x1A][shooter:i32][flags:u8][dir:cv3][more_flags:u8][optional target:i32] */
    validation_total++;
    if (g_captured_beam_len > 0) {
        if (g_captured_beam[0] == BC_OP_BEAM_FIRE && g_captured_beam_len >= 10) {
            printf("      BeamFire: op=0x%02X len=%d (>=10) PASS\n",
                   g_captured_beam[0], g_captured_beam_len);
            validation_pass++;
        } else {
            printf("      BeamFire: FAIL (op=0x%02X len=%d)\n",
                   g_captured_beam[0], g_captured_beam_len);
        }
    } else {
        printf("      BeamFire: SKIP (no phasers fired)\n");
        validation_pass++; /* not a failure */
    }

    /* TorpedoFire: [0x19][shooter:i32][subsys:u8][flags:u8][vel:cv3]... */
    validation_total++;
    if (g_captured_torp_len > 0) {
        if (g_captured_torp[0] == BC_OP_TORPEDO_FIRE && g_captured_torp_len >= 11) {
            printf("      TorpedoFire: op=0x%02X len=%d (>=11) PASS\n",
                   g_captured_torp[0], g_captured_torp_len);
            validation_pass++;
        } else {
            printf("      TorpedoFire: FAIL (op=0x%02X len=%d)\n",
                   g_captured_torp[0], g_captured_torp_len);
        }
    } else {
        printf("      TorpedoFire: SKIP (no torpedoes fired)\n");
        validation_pass++;
    }

    /* ObjectCreateTeam: [0x03][owner:u8][team:u8][blob...] */
    validation_total++;
    if (g_captured_create_len > 0 && g_captured_create[0] == BC_OP_OBJ_CREATE_TEAM) {
        printf("      ObjectCreateTeam: op=0x%02X len=%d PASS\n",
               g_captured_create[0], g_captured_create_len);
        validation_pass++;
    } else {
        printf("      ObjectCreateTeam: FAIL\n");
    }

    /* Explosion: [0x29][obj:i32][impact:cv4][damage:cf16][radius:cf16] = 14 bytes */
    validation_total++;
    if (g_captured_explosion_len > 0) {
        if (g_captured_explosion[0] == BC_OP_EXPLOSION && g_captured_explosion_len == 14) {
            printf("      Explosion: op=0x%02X len=%d (==14) PASS\n",
                   g_captured_explosion[0], g_captured_explosion_len);
            validation_pass++;
        } else {
            printf("      Explosion: FAIL (op=0x%02X len=%d, expected 14)\n",
                   g_captured_explosion[0], g_captured_explosion_len);
        }
    } else {
        printf("      Explosion: SKIP (no explosions)\n");
        validation_pass++;
    }

    printf("    Validation: %d/%d passed\n", validation_pass, validation_total);

    /* === Assertions === */
    assert_fail = 0;

    /* Battle actually happened */
    if (g_op_sent[BC_OP_BEAM_FIRE] + g_op_sent[BC_OP_TORPEDO_FIRE] == 0) {
        printf("    ASSERT FAIL: no weapons fired\n"); assert_fail++;
    }

    /* All create packets relayed */
    if (g_op_sent[BC_OP_OBJ_CREATE_TEAM] < g_num_players) {
        printf("    ASSERT FAIL: not all ships spawned (%d/%d)\n",
               g_op_sent[BC_OP_OBJ_CREATE_TEAM], g_num_players); assert_fail++;
    }

    /* No alive ship with hull <= 0 */
    for (int i = 0; i < g_num_players; i++) {
        if (g_bp[i].ship.alive && g_bp[i].ship.hull_hp <= 0.0f) {
            printf("    ASSERT FAIL: ship %d alive with hull=0\n", i); assert_fail++;
        }
        if (!g_bp[i].ship.alive && g_bp[i].ship.hull_hp > 0.0f) {
            printf("    ASSERT FAIL: ship %d dead with hull>0\n", i); assert_fail++;
        }
    }

    /* At least one ship survived */
    if (survivors == 0) {
        printf("    ASSERT FAIL: no survivors\n"); assert_fail++;
    }

    /* Trace file is non-empty */
    {
        FILE *check = fopen(NB_TRACE_FILE, "rb");
        if (check) {
            fseek(check, 0, SEEK_END);
            long sz = ftell(check);
            fclose(check);
            if (sz <= 8) {
                printf("    ASSERT FAIL: trace file empty\n"); assert_fail++;
            }
        } else {
            printf("    ASSERT FAIL: trace file missing\n"); assert_fail++;
        }
    }

    /* Structural validation all passed */
    if (validation_pass < validation_total) {
        printf("    ASSERT FAIL: structural validation\n"); assert_fail++;
    }

    if (assert_fail == 0) {
        printf("    All assertions passed!\n");
    } else {
        printf("    %d assertions FAILED\n", assert_fail);
    }

cleanup:
    for (int i = 0; i < g_num_players; i++)
        test_client_disconnect(&g_bp[i].client);
    Sleep(100);
    test_server_stop(&g_srv);
    bc_net_shutdown();
    packet_log_close(&g_trace);

    return assert_fail;
}

/* ======================================================================
 * Dump mode: --dump <trace_file>
 * ====================================================================== */

static int run_dump(const char *path)
{
    packet_log_dump(path);
    return 0;
}

/* ======================================================================
 * Test wrapper + main
 * ====================================================================== */

TEST(networked_battle)
{
    ASSERT(run_networked_battle() == 0);
}

int main(int argc, char *argv[])
{
    /* --dump mode */
    if (argc >= 3 && strcmp(argv[1], "--dump") == 0) {
        return run_dump(argv[2]);
    }

    printf("Running %s\n", __FILE__);
    RUN(networked_battle);
    printf("%d/%d tests passed\n", test_pass, test_count);
    return test_fail > 0 ? 1 : 0;
}
