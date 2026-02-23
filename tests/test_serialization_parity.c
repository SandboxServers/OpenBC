/*
 * test_serialization_parity.c -- Verify each stock ship's serialization list
 * matches the per-ship wire format spec (docs/wire-formats/per-ship-subsystem-wire-format.md).
 *
 * Checks entry count, format types per entry, and total cycle bytes.
 * Catches regressions like the Sovereign Bridge-at-index-7 bug.
 */
#include "test_util.h"
#include "openbc/ship_data.h"
#include <string.h>

#define REGISTRY_DIR  "data/vanilla-1.1"

static bc_game_registry_t g_reg;

/* Expected wire format data per stock ship, from the verified spec. */
typedef struct {
    u16 species_id;
    int entry_count;
    int cycle_bytes;
    u8  formats[BC_SS_MAX_ENTRIES];  /* BC_SS_FORMAT_BASE/POWERED/POWER per entry */
} expected_ship_t;

/* Compute cycle bytes from a serialization list.
 * Base:    1 + child_count
 * Powered: 3 + child_count  (condition + children + hasData-bit + powerPct)
 * Reactor: 3                (condition + mainBattery + backupBattery)
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
 * All 16 stock ships (Enterprise species 37 not in registry).
 * Format arrays derived from per-ship-subsystem-wire-format.md.
 */
static const expected_ship_t EXPECTED[] = {
    /* Species 1: Akira -- 11 entries, 47 bytes
     * Hull(B) ShieldGen(B) Sensors(P) WarpCore(R) Impulse(P,2) Phasers(P,8)
     * WarpEng(P,2) Torps(P,6) Engineering(P) Tractors(P,2) Bridge(B) */
    { 1, 11, 47, {0,0,1,2,1,1,1,1,1,1,0} },

    /* Species 2: Ambassador -- 11 entries, 45 bytes */
    { 2, 11, 45, {0,0,1,2,1,1,1,1,1,0,1} },

    /* Species 3: Galaxy -- 11 entries, 50 bytes
     * Hull(B) WarpCore(R) ShieldGen(B) Sensors(P) Torps(P,6) Phasers(P,8)
     * Impulse(P,3) WarpEng(P,2) Tractors(P,4) Bridge(B) Engineering(P) */
    { 3, 11, 50, {0,2,0,1,1,1,1,1,1,0,1} },

    /* Species 4: Nebula -- 11 entries, 47 bytes */
    { 4, 11, 47, {0,0,1,2,1,1,1,1,1,1,0} },

    /* Species 5: Sovereign -- 11 entries, 49 bytes
     * Hull(B) ShieldGen(B) Sensors(P) WarpCore(R) Impulse(P,2) Torps(P,6)
     * Repair(P) Phasers(P,8) Tractors(P,4) WarpEng(P,2) Bridge(B) */
    { 5, 11, 49, {0,0,1,2,1,1,1,1,1,1,0} },

    /* Species 6: Bird of Prey -- 10 entries, 32 bytes */
    { 6, 10, 32, {0,0,2,1,1,1,1,1,1,1} },

    /* Species 7: Vor'cha -- 12 entries, 44 bytes */
    { 7, 12, 44, {0,0,2,1,1,1,1,1,1,1,1,1} },

    /* Species 8: Warbird -- 13 entries, 46 bytes */
    { 8, 13, 46, {0,0,2,1,1,1,1,1,1,1,1,0,1} },

    /* Species 9: Marauder -- 10 entries, 35 bytes */
    { 9, 10, 35, {0,0,2,1,1,1,1,1,1,1} },

    /* Species 10: Galor -- 9 entries, 31 bytes */
    {10,  9, 31, {0,0,2,1,1,1,1,1,1} },

    /* Species 11: Keldon -- 10 entries, 39 bytes */
    {11, 10, 39, {0,0,2,1,1,1,1,1,1,1} },

    /* Species 12: CardHybrid -- 11 entries, 47 bytes */
    {12, 11, 47, {0,2,1,1,0,1,1,1,1,1,1} },

    /* Species 13: KessokHeavy -- 10 entries, 40 bytes */
    {13, 10, 40, {0,2,1,1,1,1,1,0,1,1} },

    /* Species 14: KessokLight -- 10 entries, 39 bytes */
    {14, 10, 39, {0,2,1,1,0,1,1,1,1,1} },

    /* Species 15: Shuttle -- 9 entries, 29 bytes */
    {15,  9, 29, {0,1,2,1,0,1,1,1,1} },
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

TEST(format_types_match)
{
    for (int i = 0; i < EXPECTED_COUNT; i++) {
        const expected_ship_t *exp = &EXPECTED[i];
        const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, exp->species_id);
        ASSERT(cls != NULL);
        for (int j = 0; j < exp->entry_count; j++) {
            if (cls->ser_list.entries[j].format != exp->formats[j]) {
                printf("FAIL\n    species %d (%s) entry %d: format=%d, expected=%d\n",
                       exp->species_id, cls->name, j,
                       cls->ser_list.entries[j].format, exp->formats[j]);
                test_fail++; test_pass--;
                return;
            }
        }
    }
}

TEST(sovereign_bridge_not_at_index_7)
{
    /* Regression: Bridge (Base, 1 byte) was at index 7 where client expects
     * Phasers (Powered, 8 children, 11 bytes). This caused cascading desync. */
    const bc_ship_class_t *cls = bc_registry_find_ship(&g_reg, 5);
    ASSERT(cls != NULL);
    ASSERT(cls->ser_list.count >= 11);

    /* Index 7 must be Powered (Phasers), not Base (Bridge) */
    ASSERT_EQ(cls->ser_list.entries[7].format, BC_SS_FORMAT_POWERED);

    /* Index 10 must be Base (Bridge) */
    ASSERT_EQ(cls->ser_list.entries[10].format, BC_SS_FORMAT_BASE);
}

/* === Run all tests === */

TEST_MAIN_BEGIN()
    RUN(load_registry);
    RUN(all_stock_ships_present);
    RUN(entry_counts_match);
    RUN(cycle_bytes_match);
    RUN(format_types_match);
    RUN(sovereign_bridge_not_at_index_7);
TEST_MAIN_END()
