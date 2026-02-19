#ifndef OPENBC_SHIP_POWER_H
#define OPENBC_SHIP_POWER_H

#include "openbc/types.h"
#include "openbc/ship_state.h"
#include "openbc/ship_data.h"

/* Build a StateUpdate with dirty=0x20 for hierarchical subsystem health.
 * Uses the ship's serialization list (ser_list) with 10-byte budget round-robin.
 * start_idx is position in ser_list.entries[]. Writes subsystems starting there,
 * wrapping at list end, stopping after 10 bytes or a full cycle.
 * Returns bytes written to buf, or 0 if nothing to send.
 * *next_idx is set to the next round-robin position for the caller to persist. */
int bc_ship_build_health_update(const bc_ship_state_t *ship,
                                 const bc_ship_class_t *cls,
                                 f32 game_time,
                                 u8 start_idx, u8 *next_idx,
                                 u8 *buf, int buf_size);

/* Tick the reactor/power simulation: generate power, distribute to consumers,
 * compute efficiency ratios. Call once per frame. */
void bc_ship_power_tick(bc_ship_state_t *ship,
                        const bc_ship_class_t *cls,
                        f32 dt);

#endif /* OPENBC_SHIP_POWER_H */
