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

/* Locate the 0x20 subsystem payload inside a built StateUpdate that carries
 * ONLY the SUBSYSTEM dirty flag (so the payload is [start_idx][entries...]).
 * Returns a pointer to the start_idx byte and the remaining length. */
static const u8 *find_sub_payload(const u8 *pkt, int len, int *out_remaining)
{
    /* layout: [op:1][obj:4][time:4][dirty:1][start_idx][...] */
    if (len < 11) return NULL;
    *out_remaining = len - 10;
    return pkt + 10;
}

TEST(per_subsystem_power_bit_is_standalone_byte)
{
    /* Each powered entry's has_power bit must occupy its OWN single-bit group
     * byte (0x20 clear / 0x21 set), broken by the surrounding condition and
     * power_pct WriteByte calls -- matching stock (#186 Bug 2).  Build a real
     * remote (is_own_ship=false) health window over a galaxy and assert the
     * powered parent emits a standalone 0x21 byte. */
    bc_game_registry_t reg;
    memset(&reg, 0, sizeof(reg));
    ASSERT(bc_registry_load_dir(&reg, "data/vanilla-1.1"));

    const bc_ship_class_t *cls = bc_registry_find_ship(&reg, 3); /* Galaxy */
    ASSERT(cls != NULL);

    /* Find the first powered entry (the round-robin will start there). */
    int powered = -1;
    for (int i = 0; i < cls->ser_list.count; i++) {
        if (cls->ser_list.entries[i].format == BC_SS_FORMAT_POWERED) {
            powered = i;
            break;
        }
    }
    ASSERT(powered >= 0);

    bc_ship_state_t ship;
    bc_ship_init(&ship, cls, 0, bc_make_ship_id(0), 1, 0);
    ship.power_pct[powered] = 80;
    ship.subsys_enabled[powered] = true;

    u8 pkt[256];
    u8 next_idx;
    int len = bc_ship_build_health_update(&ship, cls, 12.5f,
                                          (u8)powered, &next_idx,
                                          false /* remote */,
                                          pkt, sizeof(pkt));
    ASSERT(len > 0);

    int rem = 0;
    const u8 *p = find_sub_payload(pkt, len, &rem);
    ASSERT(p != NULL);
    ASSERT(rem >= 3);
    /* p[0] = start_idx, p[1] = condition byte, p[2] = standalone has_power
     * bit-group byte (count=1, bit0=1) = 0x21, p[3] = power_pct (80). */
    ASSERT_EQ_INT(p[0], powered);
    ASSERT_EQ_INT(p[2], 0x21);
    ASSERT_EQ_INT(p[3], 80);
}

TEST(flat_multi_entry_block_round_trips)
{
    /* Build a real remote health window spanning several flat entries (so it
     * crosses the powered/base boundaries the bug used to corrupt), apply it
     * on a fresh ship, and confirm the powered entries decode correctly. */
    bc_game_registry_t reg;
    memset(&reg, 0, sizeof(reg));
    ASSERT(bc_registry_load_dir(&reg, "data/vanilla-1.1"));

    const bc_ship_class_t *cls = bc_registry_find_ship(&reg, 3); /* Galaxy */
    ASSERT(cls != NULL);

    bc_ship_state_t sender;
    bc_ship_init(&sender, cls, 0, bc_make_ship_id(0), 1, 0);
    /* Set distinct power values on every powered entry. */
    for (int i = 0; i < cls->ser_list.count; i++) {
        if (cls->ser_list.entries[i].format == BC_SS_FORMAT_POWERED) {
            sender.power_pct[i] = (u8)(40 + i);
            sender.subsys_enabled[i] = true;
        }
    }

    bc_ship_state_t receiver;
    bc_ship_init(&receiver, cls, 0, bc_make_ship_id(0), 1, 0);

    /* Walk the whole round-robin cycle, applying each window. */
    u8 cursor = 0;
    int total_updated = 0;
    int safety = cls->ser_list.count + 2;
    do {
        u8 pkt[256];
        u8 next_idx;
        int len = bc_ship_build_health_update(&sender, cls, 7.0f,
                                              cursor, &next_idx,
                                              false /* remote */,
                                              pkt, sizeof(pkt));
        ASSERT(len > 0);
        int upd = bc_ship_apply_remote_power_state(pkt, len, cls, &receiver);
        total_updated += upd;
        if (next_idx == cursor) break;
        cursor = next_idx;
    } while (cursor != 0 && --safety > 0);

    ASSERT(total_updated > 0);

    /* Every powered entry's value must have round-tripped intact. */
    for (int i = 0; i < cls->ser_list.count; i++) {
        if (cls->ser_list.entries[i].format == BC_SS_FORMAT_POWERED) {
            ASSERT_EQ_INT(receiver.power_pct[i], sender.power_pct[i]);
            ASSERT(receiver.subsys_enabled[i]);
        }
    }
}

TEST_MAIN_BEGIN()
    RUN(remote_power_state_updates_percentage);
    RUN(remote_power_state_applies_sign_bit_disable);
    RUN(remote_power_state_ignores_non_subsystem_updates);
    RUN(per_subsystem_power_bit_is_standalone_byte);
    RUN(flat_multi_entry_block_round_trips);
TEST_MAIN_END()
