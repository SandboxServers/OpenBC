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
#include "openbc/json_parse.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define REGISTRY_DIR  "data/vanilla-1.1"
#define SHIPS_DIR     REGISTRY_DIR "/ships"

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

/* Write a synthetic registry JSON whose flattened serialization tree exceeds
 * BC_MAX_SUBSYSTEMS.  The ship has ZERO real subsystems, so every
 * serialization entry is a synthetic container that consumes a fresh HP slot.
 * With (BC_MAX_SUBSYSTEMS + 8) parent entries the flatten loop must saturate
 * next_hp_slot and STOP -- it must never emit hp_index == BC_MAX_SUBSYSTEMS. */
static const char *write_oversized_registry(void)
{
    static const char *path = "test_oversized_ship.json";
    FILE *f = fopen(path, "wb");
    if (!f) return NULL;

    fputs("{ \"ships\": [ { \"name\": \"oversized\", \"species_id\": 999,\n", f);
    fputs("  \"serialization_list\": [\n", f);
    int n = BC_MAX_SUBSYSTEMS + 8; /* deliberately over the slot pool */
    for (int i = 0; i < n; i++) {
        /* Each entry has a unique name not present in (empty) subsystems, so it
         * is synthetic and demands a new HP slot. */
        fprintf(f,
            "    { \"name\": \"synth_%d\", \"format\": \"base\", \"max_condition\": 100.0 }%s\n",
            i, (i + 1 < n) ? "," : "");
    }
    fputs("  ] } ] }\n", f);
    fclose(f);
    return path;
}

TEST(oversized_tree_never_emits_oob_hp_index)
{
    const char *path = write_oversized_registry();
    ASSERT(path != NULL);

    bc_game_registry_t reg;
    bool ok = bc_registry_load(&reg, path);
    remove(path);
    ASSERT(ok);
    ASSERT(reg.ship_count == 1);

    const bc_ship_class_t *cls = &reg.ships[0];
    const bc_ss_list_t *sl = &cls->ser_list;

    /* Truncated safely: never more entries than the list can hold. */
    ASSERT(sl->count <= BC_SS_MAX_ENTRIES);
    /* HP slot accounting must never exceed the backing array. */
    ASSERT(sl->total_hp_slots <= BC_MAX_SUBSYSTEMS);

    /* The core invariant: NO entry may carry an out-of-bounds hp_index.
     * subsystem_hp[] has exactly BC_MAX_SUBSYSTEMS slots (indices 0..63). */
    for (int i = 0; i < sl->count; i++) {
        int hp = sl->entries[i].hp_index;
        ASSERT(hp >= 0);
        ASSERT(hp < BC_MAX_SUBSYSTEMS);
    }
}

/* === Registry-wide regression guards (cover ALL loaded ships) ===
 *
 * The EXPECTED[] table above keys off species_id and only enumerates the 15
 * combat ships.  The two tests below instead iterate the FULL loaded registry
 * (every ship folder in the manifest, including non-combat hulls such as the
 * Cardassian freighter) so a newly added or re-authored ship cannot slip past
 * the parity checks.  They are the per-ship guards required by #205's
 * verification item: confirm the flattened order is sane for every ship, not
 * just galaxy. */

/* Independently recompute the expected FLATTENED top-level entry count straight
 * from a ship's serialization.json authoring tree: one entry per parent plus
 * one per child mount.  This is the count the loader MUST produce after
 * promoting every weapon mount / nacelle to its own top-level entry.  If a
 * future change regressed to nesting (children left under a parent), the loader
 * count would drop below this value and the test would fail. */
static int authored_flat_count(const char *ship_folder)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/%s/serialization.json", SHIPS_DIR, ship_folder);

    FILE *f = fopen(path, "rb");
    if (!f) return -1;
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    if (sz <= 0) { fclose(f); return -1; }
    char *buf = (char *)malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return -1; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    json_value_t *arr = json_parse(buf);
    free(buf);
    if (!arr || arr->type != JSON_ARRAY) { json_free(arr); return -1; }

    int total = 0;
    size_t n = json_array_len(arr);
    for (size_t i = 0; i < n; i++) {
        const json_value_t *e = json_array_get(arr, i);
        total += 1; /* the parent entry itself */
        const json_value_t *children = json_get(e, "children");
        if (children && children->type == JSON_ARRAY)
            total += (int)json_array_len(children); /* each mount -> own entry */
    }
    json_free(arr);
    return total;
}

/* The manifest folder list, matched 1:1 to load order so authored_flat_count()
 * can be cross-checked against the corresponding loaded ship class. */
static const char *const SHIP_FOLDERS[] = {
    "akira", "ambassador", "galaxy", "nebula", "sovereign", "birdofprey",
    "vorcha", "warbird", "marauder", "galor", "keldon", "cardhybrid",
    "kessokheavy", "kessoklight", "shuttle", "cardfreighter",
};
#define SHIP_FOLDER_COUNT (int)(sizeof(SHIP_FOLDERS) / sizeof(SHIP_FOLDERS[0]))

TEST(all_ships_hp_index_in_range)
{
    /* Core OOB guard: for EVERY loaded ship, no flattened entry may carry an
     * hp_index outside the addressable HP-slot range.  subsystem_hp[] has
     * exactly BC_MAX_SUBSYSTEMS slots; synthetic containers legitimately use
     * slots in [subsystem_count, total_hp_slots).  An out-of-range hp_index
     * would corrupt unrelated memory at StateUpdate time. */
    ASSERT(g_reg.ship_count > 0);
    for (int i = 0; i < g_reg.ship_count; i++) {
        const bc_ship_class_t *cls = bc_registry_get_ship(&g_reg, i);
        ASSERT(cls != NULL);
        const bc_ss_list_t *sl = &cls->ser_list;

        ASSERT(sl->count <= BC_SS_MAX_ENTRIES);
        ASSERT(sl->total_hp_slots <= BC_MAX_SUBSYSTEMS);

        for (int j = 0; j < sl->count; j++) {
            int hp = sl->entries[j].hp_index;
            if (hp < 0 || hp >= sl->total_hp_slots || hp >= BC_MAX_SUBSYSTEMS) {
                printf("FAIL\n    ship '%s' entry %d: hp_index=%d "
                       "(total_hp_slots=%d, max=%d)\n",
                       cls->name, j, hp, sl->total_hp_slots, BC_MAX_SUBSYSTEMS);
                test_fail++; test_pass--;
                return;
            }
            /* The runtime list is flat: children must already be promoted. */
            ASSERT_EQ_INT(sl->entries[j].child_count, 0);
        }
    }
}

TEST(flat_count_matches_authored_tree)
{
    /* For every ship folder, the loader's flattened entry count must equal the
     * authored (parents + mounts) count.  A regression to nesting would make
     * the loaded count smaller than the authored count -- caught here without
     * relying on the hand-maintained EXPECTED[] table. */
    ASSERT_EQ_INT(g_reg.ship_count, SHIP_FOLDER_COUNT);
    for (int i = 0; i < SHIP_FOLDER_COUNT; i++) {
        const bc_ship_class_t *cls = bc_registry_get_ship(&g_reg, i);
        ASSERT(cls != NULL);

        int authored = authored_flat_count(SHIP_FOLDERS[i]);
        ASSERT(authored > 0);

        if (cls->ser_list.count != authored) {
            printf("FAIL\n    ship '%s' (folder '%s'): loaded flat count=%d, "
                   "authored flat count=%d\n",
                   cls->name, SHIP_FOLDERS[i], cls->ser_list.count, authored);
            test_fail++; test_pass--;
            return;
        }
    }
}

/* flat_count_matches_authored_tree pairs registry index i with SHIP_FOLDERS[i],
 * which only holds if the registry loads ships in SHIP_FOLDERS order. That order
 * comes from the manifest, so verify SHIP_FOLDERS matches the manifest's ship
 * enumeration exactly -- if the manifest is reordered, fail loudly here instead
 * of silently comparing mismatched ship/folder pairs downstream. */
TEST(ship_folders_match_manifest_order)
{
    char path[512];
    snprintf(path, sizeof(path), "%s/manifest.json", REGISTRY_DIR);

    FILE *f = fopen(path, "rb");
    ASSERT(f != NULL);
    fseek(f, 0, SEEK_END);
    long sz = ftell(f);
    fseek(f, 0, SEEK_SET);
    ASSERT(sz > 0);
    char *buf = (char *)malloc((size_t)sz + 1);
    ASSERT(buf != NULL);
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    buf[rd] = '\0';

    json_value_t *root = json_parse(buf);
    free(buf);
    ASSERT(root != NULL);

    const json_value_t *ships = json_get(root, "ships");
    ASSERT(ships != NULL && ships->type == JSON_ARRAY);
    ASSERT((int)json_array_len(ships) == SHIP_FOLDER_COUNT);

    for (int i = 0; i < SHIP_FOLDER_COUNT; i++) {
        const json_value_t *e = json_array_get(ships, (size_t)i);
        ASSERT(e != NULL && e->type == JSON_STRING);
        const char *folder = json_string(e);
        if (!folder || strcmp(folder, SHIP_FOLDERS[i]) != 0) {
            printf("FAIL\n    manifest ship[%d]='%s' != SHIP_FOLDERS[%d]='%s'\n",
                   i, folder ? folder : "(null)", i, SHIP_FOLDERS[i]);
            test_fail++; test_pass--;
            json_free(root);
            return;
        }
    }
    json_free(root);
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
    RUN(oversized_tree_never_emits_oob_hp_index);
    RUN(all_ships_hp_index_in_range);
    RUN(flat_count_matches_authored_tree);
    RUN(ship_folders_match_manifest_order);
TEST_MAIN_END()
