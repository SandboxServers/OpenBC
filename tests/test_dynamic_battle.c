#include "test_util.h"
#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/movement.h"
#include "openbc/combat.h"
#include "openbc/game_builders.h"
#include "openbc/opcodes.h"
#include <string.h>
#include <math.h>
#include <stdlib.h>
#include <time.h>
#include <stdio.h>

#define REGISTRY_PATH "data\\vanilla-1.1.json"

/* === Seeded RNG (xorshift32) for deterministic replay === */

static u32 g_rng_state;

static void rng_seed(u32 seed)
{
    g_rng_state = seed ? seed : 1;
}

static u32 rng_next(void)
{
    u32 x = g_rng_state;
    x ^= x << 13;
    x ^= x >> 17;
    x ^= x << 5;
    g_rng_state = x;
    return x;
}

static int rng_range(int lo, int hi)
{
    return lo + (int)(rng_next() % (u32)(hi - lo + 1));
}

static f32 rng_float(f32 lo, f32 hi)
{
    return lo + (f32)(rng_next() % 10000) / 10000.0f * (hi - lo);
}

/* === AI States === */

#define AI_APPROACH     0
#define AI_ENGAGE       1
#define AI_EVADE        2
#define AI_REPAIR       3
#define AI_CLOAK_ATTACK 4
#define AI_DEAD         5

/* === Battle participant === */

#define MAX_PLAYERS 7

typedef struct {
    bc_ship_state_t ship;
    const bc_ship_class_t *cls;
    int ai_state;
    int target_idx;      /* index into players[] */
    f32 engage_timer;    /* time in current state */
    /* Stats */
    int phasers_fired;
    int torpedoes_fired;
    f32 damage_dealt;
    f32 damage_taken;
} battle_player_t;

static bc_game_registry_t g_reg;
static battle_player_t g_players[MAX_PLAYERS];
static int g_num_players;
static int g_tick;
static f32 g_game_time;

/* Counters */
static int g_total_phaser_fires;
static int g_total_torpedo_fires;
static int g_total_deaths;
static int g_total_cloak_cycles;
static int g_total_tractor_engages;
static int g_total_repairs;

/* === Helpers === */

static int count_alive(int team)
{
    int n = 0;
    for (int i = 0; i < g_num_players; i++) {
        if (g_players[i].ship.alive && g_players[i].ship.team_id == team) n++;
    }
    return n;
}

static int find_nearest_enemy(int idx)
{
    int best = -1;
    f32 best_dist = 1e30f;
    for (int i = 0; i < g_num_players; i++) {
        if (i == idx) continue;
        if (!g_players[i].ship.alive) continue;
        if (g_players[i].ship.team_id == g_players[idx].ship.team_id) continue;
        f32 d = bc_vec3_dist(g_players[idx].ship.pos, g_players[i].ship.pos);
        if (d < best_dist) {
            best_dist = d;
            best = i;
        }
    }
    return best;
}

static f32 distance_to_target(int idx)
{
    int ti = g_players[idx].target_idx;
    if (ti < 0 || !g_players[ti].ship.alive) return 1e30f;
    return bc_vec3_dist(g_players[idx].ship.pos, g_players[ti].ship.pos);
}

/* === AI logic === */

static void ai_update(int idx, f32 dt)
{
    battle_player_t *p = &g_players[idx];
    if (!p->ship.alive) {
        p->ai_state = AI_DEAD;
        return;
    }

    /* Re-acquire target if dead */
    if (p->target_idx < 0 || !g_players[p->target_idx].ship.alive) {
        p->target_idx = find_nearest_enemy(idx);
    }
    if (p->target_idx < 0) return; /* no enemies left */

    battle_player_t *tgt = &g_players[p->target_idx];
    f32 dist = distance_to_target(idx);
    p->engage_timer += dt;

    /* Shield health ratio */
    f32 shield_total = 0, shield_max = 0;
    for (int i = 0; i < BC_MAX_SHIELD_FACINGS; i++) {
        shield_total += p->ship.shield_hp[i];
        shield_max += p->cls->shield_hp[i];
    }
    f32 shield_ratio = (shield_max > 0) ? shield_total / shield_max : 0;

    switch (p->ai_state) {
    case AI_APPROACH:
        /* Move toward target */
        bc_ship_turn_toward(&p->ship, p->cls, tgt->ship.pos, dt);
        bc_ship_set_speed(&p->ship, p->cls, p->cls->max_speed);

        /* Transition to ENGAGE when in weapons range */
        if (dist < 80.0f) {
            p->ai_state = AI_ENGAGE;
            p->engage_timer = 0;
        }
        /* Cloakers: cloak while approaching */
        if (p->cls->can_cloak && p->ship.cloak_state == BC_CLOAK_DECLOAKED &&
            dist > 60.0f && shield_ratio > 0.5f) {
            if (bc_cloak_start(&p->ship, p->cls)) {
                g_total_cloak_cycles++;
                p->ai_state = AI_CLOAK_ATTACK;
                p->engage_timer = 0;
            }
        }
        break;

    case AI_ENGAGE:
        bc_ship_turn_toward(&p->ship, p->cls, tgt->ship.pos, dt);
        bc_ship_set_speed(&p->ship, p->cls, p->cls->max_speed * 0.6f);

        /* Fire weapons */
        {
            u8 pkt[256];

            /* Phasers: fire all available banks */
            for (int b = 0; b < p->cls->phaser_banks; b++) {
                if (bc_combat_can_fire_phaser(&p->ship, p->cls, b)) {
                    int n = bc_combat_fire_phaser(&p->ship, p->cls, b,
                                                   tgt->ship.object_id, pkt, sizeof(pkt));
                    if (n > 0) {
                        p->phasers_fired++;
                        g_total_phaser_fires++;

                        /* Apply phaser damage to target */
                        int ss = -1;
                        int cnt = 0;
                        for (int s = 0; s < p->cls->subsystem_count; s++) {
                            if (strcmp(p->cls->subsystems[s].type, "phaser") == 0 ||
                                strcmp(p->cls->subsystems[s].type, "pulse_weapon") == 0) {
                                if (cnt == b) { ss = s; break; }
                                cnt++;
                            }
                        }
                        if (ss >= 0) {
                            f32 dmg = p->cls->subsystems[ss].max_damage;
                            bc_vec3_t impact = bc_vec3_normalize(
                                bc_vec3_sub(tgt->ship.pos, p->ship.pos));
                            bc_combat_apply_damage(&tgt->ship, tgt->cls, dmg, 0.0f,
                                                   impact, false);
                            p->damage_dealt += dmg;
                            tgt->damage_taken += dmg;
                        }
                    }
                }
            }

            /* Torpedoes: fire all available tubes */
            for (int t = 0; t < p->cls->torpedo_tubes; t++) {
                if (bc_combat_can_fire_torpedo(&p->ship, p->cls, t)) {
                    bc_vec3_t dir = bc_vec3_normalize(
                        bc_vec3_sub(tgt->ship.pos, p->ship.pos));
                    int n = bc_combat_fire_torpedo(&p->ship, p->cls, t,
                                                    tgt->ship.object_id, dir, pkt, sizeof(pkt));
                    if (n > 0) {
                        p->torpedoes_fired++;
                        g_total_torpedo_fires++;

                        /* Simplified: instant hit (no flight time in simulation) */
                        /* Lookup torpedo damage from projectile registry */
                        f32 dmg = 500.0f; /* default photon */
                        for (int pr = 0; pr < g_reg.projectile_count; pr++) {
                            if (g_reg.projectiles[pr].net_type_id == p->ship.torpedo_type) {
                                dmg = g_reg.projectiles[pr].damage;
                                break;
                            }
                        }
                        bc_vec3_t impact = bc_vec3_normalize(
                            bc_vec3_sub(tgt->ship.pos, p->ship.pos));
                        f32 dmg_radius = 0.0f;
                        for (int pr2 = 0; pr2 < g_reg.projectile_count; pr2++) {
                            if (g_reg.projectiles[pr2].net_type_id == p->ship.torpedo_type) {
                                dmg_radius = g_reg.projectiles[pr2].damage *
                                             g_reg.projectiles[pr2].damage_radius_factor;
                                break;
                            }
                        }
                        bc_combat_apply_damage(&tgt->ship, tgt->cls, dmg, dmg_radius,
                                               impact, (dmg_radius > 0.0f));
                        p->damage_dealt += dmg;
                        tgt->damage_taken += dmg;
                    }
                }
            }

            /* Tractor: engage if available and in range */
            if (p->cls->has_tractor && p->ship.tractor_target_id < 0 && dist < 90.0f) {
                if (bc_combat_can_tractor(&p->ship, p->cls, 0)) {
                    bc_combat_tractor_engage(&p->ship, p->cls, 0, tgt->ship.object_id);
                    g_total_tractor_engages++;
                }
            }
        }

        /* Check for kill */
        if (!tgt->ship.alive) {
            g_total_deaths++;
            /* Release tractor if targeting dead ship */
            if (p->ship.tractor_target_id == tgt->ship.object_id)
                bc_combat_tractor_disengage(&p->ship);
            p->target_idx = find_nearest_enemy(idx);
            p->ai_state = AI_APPROACH;
            p->engage_timer = 0;
        }

        /* Evade when shields low */
        if (shield_ratio < 0.2f) {
            p->ai_state = AI_EVADE;
            p->engage_timer = 0;
            bc_combat_tractor_disengage(&p->ship);
        }
        break;

    case AI_EVADE:
        /* Turn away from target and run */
        {
            bc_vec3_t away = bc_vec3_normalize(
                bc_vec3_sub(p->ship.pos, tgt->ship.pos));
            bc_vec3_t flee_target = bc_vec3_add(p->ship.pos, bc_vec3_scale(away, 100.0f));
            bc_ship_turn_toward(&p->ship, p->cls, flee_target, dt);
            bc_ship_set_speed(&p->ship, p->cls, p->cls->max_speed);
        }

        /* Try to repair while evading */
        bc_repair_auto_queue(&p->ship, p->cls);
        bc_repair_tick(&p->ship, p->cls, dt);
        if (p->ship.repair_count > 0) g_total_repairs++;

        /* Return to combat when shields recover */
        if (shield_ratio > 0.5f || p->engage_timer > 10.0f) {
            p->ai_state = AI_APPROACH;
            p->engage_timer = 0;
        }
        break;

    case AI_REPAIR:
        bc_repair_auto_queue(&p->ship, p->cls);
        bc_repair_tick(&p->ship, p->cls, dt);
        if (p->ship.repair_count > 0) g_total_repairs++;

        if (p->engage_timer > 5.0f || p->ship.repair_count == 0) {
            p->ai_state = AI_APPROACH;
            p->engage_timer = 0;
        }
        break;

    case AI_CLOAK_ATTACK:
        /* Approach cloaked, decloak at close range, alpha strike */
        bc_cloak_tick(&p->ship, dt);

        if (p->ship.cloak_state == BC_CLOAK_CLOAKING) {
            /* Wait for cloak to complete */
            bc_ship_turn_toward(&p->ship, p->cls, tgt->ship.pos, dt);
            bc_ship_set_speed(&p->ship, p->cls, p->cls->max_speed * 0.8f);
        } else if (p->ship.cloak_state == BC_CLOAK_CLOAKED) {
            /* Close distance while cloaked */
            bc_ship_turn_toward(&p->ship, p->cls, tgt->ship.pos, dt);
            bc_ship_set_speed(&p->ship, p->cls, p->cls->max_speed);

            /* Decloak at close range for alpha strike */
            if (dist < 40.0f) {
                bc_cloak_stop(&p->ship);
            }
        } else if (p->ship.cloak_state == BC_CLOAK_DECLOAKING) {
            /* Wait for decloak, then engage */
            bc_ship_turn_toward(&p->ship, p->cls, tgt->ship.pos, dt);
        } else {
            /* Fully decloaked -- transition to engage */
            p->ai_state = AI_ENGAGE;
            p->engage_timer = 0;
        }
        break;

    default:
        break;
    }
}

/* === Main battle simulation === */

TEST(dynamic_battle)
{
    /* Load registry */
    ASSERT(bc_registry_load(&g_reg, REGISTRY_PATH));

    /* Seed RNG */
    u32 seed = (u32)time(NULL);
    rng_seed(seed);
    printf("    SEED: %u\n", seed);

    /* Pick number of players: 3-7 */
    g_num_players = rng_range(3, MAX_PLAYERS);
    printf("    Players: %d\n", g_num_players);

    /* Reset counters */
    g_total_phaser_fires = 0;
    g_total_torpedo_fires = 0;
    g_total_deaths = 0;
    g_total_cloak_cycles = 0;
    g_total_tractor_engages = 0;
    g_total_repairs = 0;
    g_tick = 0;
    g_game_time = 0.0f;

    /* Pick random ships and assign teams round-robin */
    for (int i = 0; i < g_num_players; i++) {
        int ship_idx = rng_range(0, g_reg.ship_count - 1);
        const bc_ship_class_t *cls = &g_reg.ships[ship_idx];
        g_players[i].cls = cls;
        bc_ship_init(&g_players[i].ship, cls, ship_idx,
                     bc_make_ship_id(i), (u8)i, (u8)(i % 2));

        /* Random spawn position in 200-unit cube (closer for faster engagement) */
        g_players[i].ship.pos = (bc_vec3_t){
            rng_float(-100.0f, 100.0f),
            rng_float(-100.0f, 100.0f),
            rng_float(-100.0f, 100.0f),
        };

        /* Random forward direction */
        g_players[i].ship.fwd = bc_vec3_normalize((bc_vec3_t){
            rng_float(-1.0f, 1.0f),
            rng_float(-1.0f, 1.0f),
            rng_float(-1.0f, 1.0f),
        });
        /* Ensure non-zero */
        if (bc_vec3_len(g_players[i].ship.fwd) < 0.01f)
            g_players[i].ship.fwd = (bc_vec3_t){0, 1, 0};
        g_players[i].ship.fwd = bc_vec3_normalize(g_players[i].ship.fwd);

        g_players[i].ai_state = AI_APPROACH;
        g_players[i].target_idx = -1;
        g_players[i].engage_timer = 0;
        g_players[i].phasers_fired = 0;
        g_players[i].torpedoes_fired = 0;
        g_players[i].damage_dealt = 0;
        g_players[i].damage_taken = 0;

        printf("    [%d] %s (team %d) hull=%.0f\n",
               i, cls->name, i % 2, cls->hull_hp);
    }

    /* Verify all ship inits */
    for (int i = 0; i < g_num_players; i++) {
        ASSERT(g_players[i].ship.alive);
        ASSERT(g_players[i].ship.hull_hp > 0.0f);
        ASSERT(g_players[i].cls != NULL);
    }

    /* === Simulation loop: 10 Hz, max 600 ticks (60 sec) === */

    f32 dt = 0.1f;
    int max_ticks = 1200; /* 120 seconds */

    for (g_tick = 0; g_tick < max_ticks; g_tick++) {
        g_game_time = g_tick * dt;

        /* Check win condition */
        int team0 = count_alive(0);
        int team1 = count_alive(1);
        if (team0 == 0 || team1 == 0) break;

        for (int i = 0; i < g_num_players; i++) {
            if (!g_players[i].ship.alive) continue;

            /* Movement */
            bc_ship_move_tick(&g_players[i].ship, dt);

            /* Weapon charge/cooldown */
            bc_combat_charge_tick(&g_players[i].ship, g_players[i].cls, 1.0f, dt);
            bc_combat_torpedo_tick(&g_players[i].ship, g_players[i].cls, dt);

            /* Shield recharge */
            bc_combat_shield_tick(&g_players[i].ship, g_players[i].cls, 1.0f, dt);

            /* Cloak tick */
            bc_cloak_tick(&g_players[i].ship, dt);

            /* Tractor tick */
            if (g_players[i].ship.tractor_target_id >= 0) {
                /* Find the target */
                for (int j = 0; j < g_num_players; j++) {
                    if (g_players[j].ship.object_id == g_players[i].ship.tractor_target_id) {
                        bc_combat_tractor_tick(&g_players[i].ship, &g_players[j].ship,
                                               g_players[i].cls, dt);
                        break;
                    }
                }
            }

            /* AI decision */
            ai_update(i, dt);
        }
    }

    /* === Print summary === */

    printf("    --- Battle Summary (tick %d / %.1fs) ---\n", g_tick, g_game_time);

    int survivors = 0;
    for (int i = 0; i < g_num_players; i++) {
        const char *status = g_players[i].ship.alive ? "ALIVE" : "DEAD";
        printf("    [%d] %s %s hull=%.0f phasers=%d torps=%d dmg_dealt=%.0f dmg_taken=%.0f\n",
               i, g_players[i].cls->name, status,
               g_players[i].ship.hull_hp,
               g_players[i].phasers_fired, g_players[i].torpedoes_fired,
               g_players[i].damage_dealt, g_players[i].damage_taken);
        if (g_players[i].ship.alive) survivors++;
    }

    printf("    Totals: phasers=%d torps=%d deaths=%d cloaks=%d tractors=%d repairs=%d\n",
           g_total_phaser_fires, g_total_torpedo_fires, g_total_deaths,
           g_total_cloak_cycles, g_total_tractor_engages, g_total_repairs);

    /* Determine winner */
    int team0 = count_alive(0);
    int team1 = count_alive(1);
    if (team0 > 0 && team1 == 0)
        printf("    WINNER: Team 0\n");
    else if (team1 > 0 && team0 == 0)
        printf("    WINNER: Team 1\n");
    else
        printf("    DRAW (time limit)\n");

    /* === Assertions === */

    /* Battle actually happened */
    ASSERT(g_total_phaser_fires + g_total_torpedo_fires > 0);

    /* At least one death or significant damage occurred */
    ASSERT(g_total_deaths > 0 || g_total_phaser_fires > 5);

    /* No ship at 0 hull is still alive */
    for (int i = 0; i < g_num_players; i++) {
        if (g_players[i].ship.hull_hp <= 0.0f) {
            ASSERT(!g_players[i].ship.alive);
        }
        if (g_players[i].ship.alive) {
            ASSERT(g_players[i].ship.hull_hp > 0.0f);
        }
    }

    /* Every ship that fired phasers should have dealt damage */
    for (int i = 0; i < g_num_players; i++) {
        if (g_players[i].phasers_fired > 0 || g_players[i].torpedoes_fired > 0) {
            ASSERT(g_players[i].damage_dealt > 0.0f);
        }
    }

    /* Damage conservation: total dealt >= total deaths (rough check) */
    f32 total_dealt = 0;
    for (int i = 0; i < g_num_players; i++)
        total_dealt += g_players[i].damage_dealt;
    ASSERT(total_dealt > 0.0f);

    /* At least one ship survived */
    ASSERT(survivors > 0);

    /* ObjectCreateTeam verification: can build packet for every ship */
    for (int i = 0; i < g_num_players; i++) {
        u8 pkt[512];
        int n = bc_ship_build_create_packet(&g_players[i].ship,
                                              g_players[i].cls, pkt, sizeof(pkt));
        ASSERT(n > 5); /* opcode + owner + team + blob */
        ASSERT_EQ(pkt[0], 0x03); /* ObjectCreateTeam opcode */
    }

    /* StateUpdate verification: can build for every alive ship */
    for (int i = 0; i < g_num_players; i++) {
        if (!g_players[i].ship.alive) continue;
        /* Create a "previous" state at origin for comparison */
        bc_ship_state_t prev;
        bc_ship_init(&prev, g_players[i].cls, g_players[i].ship.class_index,
                     g_players[i].ship.object_id, g_players[i].ship.owner_slot,
                     g_players[i].ship.team_id);
        u8 pkt[256];
        int n = bc_ship_build_state_update(&g_players[i].ship, &prev,
                                             g_game_time, pkt, sizeof(pkt));
        /* Should have dirty flags since position changed */
        ASSERT(n > 0);
        ASSERT_EQ(pkt[0], 0x1C); /* StateUpdate opcode */
    }

    /* Weapon fire packet format: verify BeamFire and TorpedoFire can build */
    {
        u8 pkt[256];
        int n = bc_build_beam_fire(pkt, sizeof(pkt),
                                    bc_make_ship_id(0), 0,
                                    0.0f, 1.0f, 0.0f, true, bc_make_ship_id(1));
        ASSERT(n > 0);
        ASSERT_EQ(pkt[0], 0x1A);

        n = bc_build_torpedo_fire(pkt, sizeof(pkt),
                                   bc_make_ship_id(0), 0,
                                   0.0f, 1.0f, 0.0f, true, bc_make_ship_id(1),
                                   0, 0, 0);
        ASSERT(n > 0);
        ASSERT_EQ(pkt[0], 0x19);
    }

    /* DestroyObject for dead ships */
    for (int i = 0; i < g_num_players; i++) {
        if (!g_players[i].ship.alive) {
            u8 pkt[32];
            int n = bc_build_destroy_obj(pkt, sizeof(pkt), g_players[i].ship.object_id);
            ASSERT(n == 5);
            ASSERT_EQ(pkt[0], 0x14);
        }
    }
}

/* === Specific scenario tests === */

TEST(galaxy_vs_shuttle_asymmetric)
{
    /* Galaxy should crush a Shuttle in single combat */
    ASSERT(g_reg.loaded);

    const bc_ship_class_t *galaxy_cls = bc_registry_find_ship(&g_reg, 3);
    const bc_ship_class_t *shuttle_cls = bc_registry_find_ship(&g_reg, 16);
    ASSERT(galaxy_cls != NULL);
    ASSERT(shuttle_cls != NULL);

    bc_ship_state_t galaxy, shuttle;
    bc_ship_init(&galaxy, galaxy_cls, 2, bc_make_ship_id(0), 0, 0);
    bc_ship_init(&shuttle, shuttle_cls, 15, bc_make_ship_id(1), 1, 1);

    /* Place in weapons range, facing each other */
    galaxy.pos = (bc_vec3_t){0, 0, 0};
    galaxy.fwd = (bc_vec3_t){0, 1, 0};
    shuttle.pos = (bc_vec3_t){0, 30, 0};
    shuttle.fwd = (bc_vec3_t){0, -1, 0};

    /* Simulate 1000 ticks of mutual fire at 10 Hz (100 seconds) */
    for (int tick = 0; tick < 1000; tick++) {
        f32 dt = 0.1f;
        bc_combat_charge_tick(&galaxy, galaxy_cls, 1.0f, dt);
        bc_combat_charge_tick(&shuttle, shuttle_cls, 1.0f, dt);
        bc_combat_torpedo_tick(&galaxy, galaxy_cls, dt);
        bc_combat_torpedo_tick(&shuttle, shuttle_cls, dt);
        bc_combat_shield_tick(&galaxy, galaxy_cls, 1.0f, dt);
        bc_combat_shield_tick(&shuttle, shuttle_cls, 1.0f, dt);

        u8 pkt[256];

        /* Galaxy fires at shuttle */
        for (int b = 0; b < galaxy_cls->phaser_banks; b++) {
            if (bc_combat_can_fire_phaser(&galaxy, galaxy_cls, b)) {
                bc_combat_fire_phaser(&galaxy, galaxy_cls, b,
                                      shuttle.object_id, pkt, sizeof(pkt));
                /* Find phaser damage */
                int cnt = 0;
                for (int s = 0; s < galaxy_cls->subsystem_count; s++) {
                    if (strcmp(galaxy_cls->subsystems[s].type, "phaser") == 0 ||
                        strcmp(galaxy_cls->subsystems[s].type, "pulse_weapon") == 0) {
                        if (cnt == b) {
                            bc_vec3_t imp = {0, 1, 0};
                            bc_combat_apply_damage(&shuttle, shuttle_cls,
                                                    galaxy_cls->subsystems[s].max_damage,
                                                    0.0f, imp, false);
                            break;
                        }
                        cnt++;
                    }
                }
            }
        }

        /* Galaxy torpedoes at shuttle */
        for (int t = 0; t < galaxy_cls->torpedo_tubes; t++) {
            if (bc_combat_can_fire_torpedo(&galaxy, galaxy_cls, t)) {
                bc_vec3_t dir = {0, 1, 0};
                bc_combat_fire_torpedo(&galaxy, galaxy_cls, t,
                                        shuttle.object_id, dir, pkt, sizeof(pkt));
                /* Photon torpedo damage = 500, area-effect */
                bc_vec3_t imp = {0, 1, 0};
                bc_combat_apply_damage(&shuttle, shuttle_cls, 500.0f, 500.0f,
                                        imp, true);
            }
        }

        /* Shuttle fires at galaxy */
        for (int b = 0; b < shuttle_cls->phaser_banks; b++) {
            if (bc_combat_can_fire_phaser(&shuttle, shuttle_cls, b)) {
                bc_combat_fire_phaser(&shuttle, shuttle_cls, b,
                                      galaxy.object_id, pkt, sizeof(pkt));
                int cnt = 0;
                for (int s = 0; s < shuttle_cls->subsystem_count; s++) {
                    if (strcmp(shuttle_cls->subsystems[s].type, "phaser") == 0 ||
                        strcmp(shuttle_cls->subsystems[s].type, "pulse_weapon") == 0) {
                        if (cnt == b) {
                            bc_vec3_t imp = {0, -1, 0};
                            bc_combat_apply_damage(&galaxy, galaxy_cls,
                                                    shuttle_cls->subsystems[s].max_damage,
                                                    0.0f, imp, false);
                            break;
                        }
                        cnt++;
                    }
                }
            }
        }

        if (!shuttle.alive) break;
    }

    /* Shuttle should be dead, Galaxy should be alive with plenty of hull */
    ASSERT(!shuttle.alive);
    ASSERT(galaxy.alive);
    ASSERT(galaxy.hull_hp > galaxy_cls->hull_hp * 0.3f); /* Galaxy barely scratched */
}

TEST(bop_cloak_attack_cycle)
{
    /* Verify Bird of Prey can execute full cloak attack cycle */
    ASSERT(g_reg.loaded);

    const bc_ship_class_t *bop_cls = bc_registry_find_ship(&g_reg, 6);
    ASSERT(bop_cls != NULL);
    ASSERT(bop_cls->can_cloak);

    bc_ship_state_t bop;
    bc_ship_init(&bop, bop_cls, 5, bc_make_ship_id(0), 0, 0);

    /* Save initial shield HP for comparison */
    f32 initial_shield = bop.shield_hp[0];
    ASSERT(initial_shield > 0.0f);

    /* 1. Cloak (shields preserved per spec, just functionally disabled) */
    ASSERT(bc_cloak_start(&bop, bop_cls));
    ASSERT_EQ((int)bop.cloak_state, BC_CLOAK_CLOAKING);
    ASSERT(bop.shield_hp[0] == initial_shield); /* shields preserved */

    /* Cannot fire while cloaking */
    ASSERT(!bc_combat_can_fire_phaser(&bop, bop_cls, 0));

    /* 2. Complete cloak */
    bc_cloak_tick(&bop, BC_CLOAK_TRANSITION_TIME + 0.1f);
    ASSERT_EQ((int)bop.cloak_state, BC_CLOAK_CLOAKED);

    /* 3. Approach (just verify movement works while cloaked) */
    bc_vec3_t target_pos = {0, 100, 0};
    bc_ship_turn_toward(&bop, bop_cls, target_pos, 0.1f);
    bc_ship_set_speed(&bop, bop_cls, bop_cls->max_speed);
    bc_ship_move_tick(&bop, 1.0f); /* move forward */

    /* 4. Decloak for alpha strike */
    ASSERT(bc_cloak_stop(&bop));
    ASSERT_EQ((int)bop.cloak_state, BC_CLOAK_DECLOAKING);

    /* Vulnerability window: visible but can't fire */
    ASSERT(!bc_combat_can_fire_phaser(&bop, bop_cls, 0));

    /* 5. Complete decloak */
    bc_cloak_tick(&bop, BC_CLOAK_TRANSITION_TIME + 0.1f);
    ASSERT_EQ((int)bop.cloak_state, BC_CLOAK_DECLOAKED);

    /* 6. Now can fire (alpha strike) */
    ASSERT(bc_combat_can_fire_phaser(&bop, bop_cls, 0));

    /* Shields were preserved and can now recharge */
    bc_combat_shield_tick(&bop, bop_cls, 1.0f, 1.0f);
    ASSERT(bop.shield_hp[0] > 0.0f); /* shields active */
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    RUN(dynamic_battle);
    RUN(galaxy_vs_shuttle_asymmetric);
    RUN(bop_cloak_attack_cycle);
TEST_MAIN_END()
