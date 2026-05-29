/*
 * test_serialization_parity.c -- Verify each stock ship's FLATTENED runtime
 * serialization list matches stock's flag-0x20 round-robin.
 *
 * Stock flattens the hardpoint tree before StateUpdate runs (#186): every
 * weapon mount / engine nacelle is its OWN top-level round-robin entry, so
 * start_idx legitimately lands on individual weapon indices (6,7,8,...).  This
 * test verifies the flattened entry counts and total cycle bytes.
 *
 * Cycle bytes are INVARIANT under flattening (a nested Powered parent with N
 * children = parent[3B] + N children[1B] = 3+N, identical to N flat BASE
 * entries after a Powered parent), so the byte budget the receiver windows on
 * is unchanged -- only the top-level entry COUNT grows.
 */
#include "test_util.h"
#include "openbc/ship_data.h"
#include <string.h>

#define REGISTRY_DIR  "data/vanilla-1.1"

static bc_game_registry_t g_reg;

/* Expected wire format data per stock ship. */
typedef struct {
    u16 species_id;
    int entry_count;   /* FLATTENED top-level entry count */
    int cycle_bytes;   /* total bytes for one full round-robin cycle */
} expected_ship_t;

/* Compute cycle bytes from a serialization list.
 * The runtime list is FLAT, so every entry contributes its own bytes:
 *   Base:    1            (condition)
 *   Powered: 3            (condition + standalone has_power bit-byte + powerPct)
 *   Reactor: 3            (condition + mainBattery + backupBattery)
 * (child_count is always 0 in the flattened runtime list.)
 */
static int compute_cycle_bytes(const bc_ss_list_t *sl)
{
    int total = 0;
    for (int i = 0; i < sl->count; i++) {
        const bc_ss_entry_t *e = &sl->entries[i];
        switch (e->format) {
        case BC_SS_FORMAT_BASE:
            total += 1 + e->child_count;
            break;
        case BC_SS_FORMAT_POWERED:
            total += 3 + e->child_count;
            break;
        case BC_SS_FORMAT_POWER:
            total += 3;
            break;
        }
    }
    return total;
}

/*
 * All 15 stock ships (Enterprise species 37 not in registry).
 * Flattened entry counts measured from the flattened runtime ser_list; cycle
 * bytes are invariant under flattening (cross-checked against the prior
 * nested values: galaxy 50, sovereign 49, etc.).
 */
static const expected_ship_t EXPECTED[] = {
    { 1, 31, 47 },  /* Akira */
    { 2, 29, 45 },  /* Ambassador */
    { 3, 34, 50 },  /* Galaxy */
    { 4, 31, 47 },  /* Nebula */
    { 5, 33, 49 },  /* Sovereign */
    { 6, 16, 32 },  /* Bird of Prey */
    { 7, 24, 44 },  /* Vor'cha */
    { 8, 26, 46 },  /* Warbird */
    { 9, 19, 35 },  /* Marauder */
    {10, 17, 31 },  /* Galor */
    {11, 23, 39 },  /* Keldon */
    {12, 29, 47 },  /* CardHybrid */
    {13, 24, 40 },  /* KessokHeavy */
    {14, 23, 39 },  /* KessokLight */
    {15, 15, 29 },  /* Shuttle */
};

#define EXPECTED_COUNT  (int)(sizeof(EXPECTED) / sizeof(EXPECTED[0]))

/* === Tests === */

TEST(load_registry)
{
    ASSERT(bc_registry_load_dir(&g_reg, REGISTRY_DIR));
    ASSERT(g_reg.loaded);
}

TEST(all_stock_ships_present)
{
    for (int i = 0; i < EXPECTED_COUNT; i++) {
        const expected_ship_t *exp = &EXPECTED[i];
        const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, exp->species_id);
        ASSERT(cls != NULL);
    }
}

TEST(entry_counts_match)
{
    for (int i = 0; i < EXPECTED_COUNT; i++) {
        const expected_ship_t *exp = &EXPECTED[i];
        const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, exp->species_id);
        ASSERT(cls != NULL);
        if (cls->ser_list.count != exp->entry_count) {
            printf("FAIL\n    species %d (%s): entry_count=%d, expected=%d\n",
                   exp->species_id, cls->name, cls->ser_list.count, exp->entry_count);
            test_fail++; test_pass--;
            return;
        }
    }
}

TEST(cycle_bytes_match)
{
    for (int i = 0; i < EXPECTED_COUNT; i++) {
        const expected_ship_t *exp = &EXPECTED[i];
        const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, exp->species_id);
        ASSERT(cls != NULL);
        int actual = compute_cycle_bytes(&cls->ser_list);
        if (actual != exp->cycle_bytes) {
            printf("FAIL\n    species %d (%s): cycle_bytes=%d, expected=%d\n",
                   exp->species_id, cls->name, actual, exp->cycle_bytes);
            test_fail++; test_pass--;
            return;
        }
    }
}

TEST(runtime_list_is_flat)
{
    /* After flattening, NO runtime entry retains inline children -- every
     * weapon mount / nacelle is its own top-level entry (#186). */
    for (int i = 0; i < EXPECTED_COUNT; i++) {
        const expected_ship_t *exp = &EXPECTED[i];
        const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, exp->species_id);
        ASSERT(cls != NULL);
        for (int j = 0; j < cls->ser_list.count; j++) {
            ASSERT_EQ_INT(cls->ser_list.entries[j].child_count, 0);
        }
    }
}

TEST(every_weapon_mount_is_top_level)
{
    /* Galaxy has 6 torpedo tubes + 8 phasers + 4 tractors = 18 weapon mounts,
     * each of which must appear as its OWN top-level ser_list entry (BASE).
     * Under the old nested model these collapsed into 3 powered parents. */
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);

    int torps = 0, phasers = 0, tractors = 0;
    for (int i = 0; i < cls->ser_list.count; i++) {
        int hp = cls->ser_list.entries[i].hp_index;
        if (hp < 0 || hp >= cls->subsystem_count) continue;
        const char *ty = cls->subsystems[hp].type;
        if (strcmp(ty, "torpedo_tube") == 0) torps++;
        else if (strcmp(ty, "phaser") == 0) phasers++;
        else if (strcmp(ty, "tractor_beam") == 0) tractors++;
    }
    ASSERT_EQ_INT(torps, 6);
    ASSERT_EQ_INT(phasers, 8);
    ASSERT_EQ_INT(tractors, 4);

    /* The flattened galaxy has many more top-level entries than the 11 of the
     * old nested model -- proving weapons are no longer collapsed. */
    ASSERT(cls->ser_list.count > 11);
}

TEST(start_idx_reaches_weapon_indices)
{
    /* Wire proof for #186: stock start_idx legitimately lands on 6-11 (the
     * weapon-mount range).  This is only possible if those mounts are distinct
     * top-level entries.  Verify galaxy has valid (in-range) entries at
     * indices 6..11 and that they are weapon mounts, not a collapsed parent. */
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 3);
    ASSERT(cls != NULL);
    ASSERT(cls->ser_list.count > 11);

    /* Galaxy flat layout (AddToSet order):
     *   0 Hull, 1 WarpCore, 2 ShieldGen, 3 Sensor,
     *   4 Torpedoes(parent), 5-10 six torpedo tubes,
     *   11 Phasers(parent), 12-19 eight phasers, ...
     * Indices 6-10 are individual torpedo tubes -- impossible to reach as a
     * start_idx if they were collapsed under one parent.  This is the #186
     * wire proof: start_idx legitimately lands in this range. */
    for (int idx = 6; idx <= 10; idx++) {
        int hp = cls->ser_list.entries[idx].hp_index;
        ASSERT(hp >= 0 && hp < cls->subsystem_count);
        ASSERT_EQ_INT(strcmp(cls->subsystems[hp].type, "torpedo_tube"), 0);
    }
    /* And the phaser mounts sit at the higher indices (12+), each its own
     * top-level entry. */
    int phasers_at_high_idx = 0;
    for (int idx = 12; idx < cls->ser_list.count; idx++) {
        int hp = cls->ser_list.entries[idx].hp_index;
        if (hp >= 0 && hp < cls->subsystem_count &&
            strcmp(cls->subsystems[hp].type, "phaser") == 0)
            phasers_at_high_idx++;
    }
    ASSERT_EQ_INT(phasers_at_high_idx, 8);
}

TEST(reactor_entry_format_is_power)
{
    /* The recorded reactor entry index must still point at the Power entry
     * after flattening (the reactor index is recomputed to the flat position). */
    for (int i = 0; i < EXPECTED_COUNT; i++) {
        const expected_ship_t *exp = &EXPECTED[i];
        const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, exp->species_id);
        ASSERT(cls != NULL);
        int ri = cls->ser_list.reactor_entry_idx;
        ASSERT(ri >= 0 && ri < cls->ser_list.count);
        ASSERT_EQ(cls->ser_list.entries[ri].format, BC_SS_FORMAT_POWER);
    }
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    RUN(load_registry);
    RUN(all_stock_ships_present);
    RUN(entry_counts_match);
    RUN(cycle_bytes_match);
    RUN(runtime_list_is_flat);
    RUN(every_weapon_mount_is_top_level);
    RUN(start_idx_reaches_weapon_indices);
    RUN(reactor_entry_format_is_power);
TEST_MAIN_END()
