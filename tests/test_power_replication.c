#include "test_util.h"

#include "openbc/ship_data.h"
#include "openbc/ship_state.h"
#include "openbc/ship_power.h"
#include "openbc/game_builders.h"
#include "openbc/buffer.h"
#include "openbc/opcodes.h"

#include <string.h>

static int find_first_powered_entry(const bc_ship_class_t *cls)
{
    for (int i = 0; i < cls->ser_list.count; i++) {
        if (cls->ser_list.entries[i].format == BC_SS_FORMAT_POWERED)
            return i;
    }
    return -1;
}

static const bc_ship_class_t *pick_ship_with_power(const bc_game_registry_t *reg,
                                                   int *entry_idx_out)
{
    for (int i = 0; i < reg->ship_count; i++) {
        int entry = find_first_powered_entry(&reg->ships[i]);
        if (entry >= 0) {
            *entry_idx_out = entry;
            return &reg->ships[i];
        }
    }
    *entry_idx_out = -1;
    return NULL;
}

static int build_single_entry_power_update(const bc_ship_state_t *ship,
                                           const bc_ship_class_t *cls,
                                           int entry_idx,
                                           u8 power_byte,
                                           u8 *out,
                                           int out_size)
{
    const bc_ss_entry_t *e = &cls->ser_list.entries[entry_idx];
    if (e->format != BC_SS_FORMAT_POWERED) return -1;

    u8 fields[128];
    bc_buffer_t fb;
    bc_buf_init(&fb, fields, sizeof(fields));

    if (!bc_buf_write_u8(&fb, (u8)entry_idx)) return -1; /* start_idx */
    if (!bc_buf_write_u8(&fb, 0xFF)) return -1;          /* condition */
    for (int c = 0; c < e->child_count; c++) {
        if (!bc_buf_write_u8(&fb, 0xFF)) return -1;      /* child conditions */
    }
    if (!bc_buf_write_bit(&fb, true)) return -1;         /* has_power_data */
    if (!bc_buf_write_u8(&fb, power_byte)) return -1;    /* pct/sign-bit */

    return bc_build_state_update(out, out_size,
                                 ship->object_id, 12.5f,
                                 BC_DIRTY_SUBSYSTEM_STATES,
                                 fields, (int)fb.pos);
}

TEST(remote_power_state_updates_percentage)
{
    bc_game_registry_t reg;
    memset(&reg, 0, sizeof(reg));
    ASSERT(bc_registry_load_dir(&reg, "data/vanilla-1.1"));

    int entry_idx = -1;
    const bc_ship_class_t *cls = pick_ship_with_power(&reg, &entry_idx);
    ASSERT(cls != NULL);
    ASSERT(entry_idx >= 0);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 0, bc_make_ship_id(0), 1, 0);

    u8 pkt[256];
    int len = build_single_entry_power_update(&ship, cls, entry_idx, 55,
                                              pkt, sizeof(pkt));
    ASSERT(len > 0);

    ship.power_pct[entry_idx] = 100;
    ship.subsys_enabled[entry_idx] = true;

    int updated = bc_ship_apply_remote_power_state(pkt, len, cls, &ship);
    ASSERT_EQ_INT(updated, 1);
    ASSERT_EQ_INT(ship.power_pct[entry_idx], 55);
    ASSERT(ship.subsys_enabled[entry_idx]);
}

TEST(remote_power_state_applies_sign_bit_disable)
{
    bc_game_registry_t reg;
    memset(&reg, 0, sizeof(reg));
    ASSERT(bc_registry_load_dir(&reg, "data/vanilla-1.1"));

    int entry_idx = -1;
    const bc_ship_class_t *cls = pick_ship_with_power(&reg, &entry_idx);
    ASSERT(cls != NULL);
    ASSERT(entry_idx >= 0);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 0, bc_make_ship_id(0), 1, 0);

    u8 pkt[256];
    u8 off_40 = (u8)(-(i8)40);
    int len = build_single_entry_power_update(&ship, cls, entry_idx, off_40,
                                              pkt, sizeof(pkt));
    ASSERT(len > 0);

    ship.power_pct[entry_idx] = 100;
    ship.subsys_enabled[entry_idx] = true;

    int updated = bc_ship_apply_remote_power_state(pkt, len, cls, &ship);
    ASSERT_EQ_INT(updated, 1);
    ASSERT_EQ_INT(ship.power_pct[entry_idx], 40);
    ASSERT(!ship.subsys_enabled[entry_idx]);
}

TEST(remote_power_state_ignores_non_subsystem_updates)
{
    bc_game_registry_t reg;
    memset(&reg, 0, sizeof(reg));
    ASSERT(bc_registry_load_dir(&reg, "data/vanilla-1.1"));

    int entry_idx = -1;
    const bc_ship_class_t *cls = pick_ship_with_power(&reg, &entry_idx);
    ASSERT(cls != NULL);
    ASSERT(entry_idx >= 0);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 0, bc_make_ship_id(0), 1, 0);

    u8 fields[16];
    bc_buffer_t fb;
    bc_buf_init(&fb, fields, sizeof(fields));
    ASSERT(bc_buf_write_cf16(&fb, 12.0f)); /* speed field */

    u8 pkt[64];
    int len = bc_build_state_update(pkt, sizeof(pkt),
                                    ship.object_id, 3.0f,
                                    BC_DIRTY_SPEED,
                                    fields, (int)fb.pos);
    ASSERT(len > 0);

    ship.power_pct[entry_idx] = 77;
    ship.subsys_enabled[entry_idx] = true;

    int updated = bc_ship_apply_remote_power_state(pkt, len, cls, &ship);
    ASSERT_EQ_INT(updated, 0);
    ASSERT_EQ_INT(ship.power_pct[entry_idx], 77);
    ASSERT(ship.subsys_enabled[entry_idx]);
}

TEST_MAIN_BEGIN()
    RUN(remote_power_state_updates_percentage);
    RUN(remote_power_state_applies_sign_bit_disable);
    RUN(remote_power_state_ignores_non_subsystem_updates);
TEST_MAIN_END()
