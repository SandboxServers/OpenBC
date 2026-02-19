#ifndef OPENBC_SERVER_DISPATCH_H
#define OPENBC_SERVER_DISPATCH_H

#include "openbc/types.h"
#include "openbc/net.h"
#include "openbc/ship_state.h"
#include "openbc/ship_data.h"

/* Handle an incoming game packet (decrypt, parse transport, dispatch). */
void bc_handle_packet(const bc_addr_t *from, u8 *data, int len);

/* Handle a GameSpy query or master server challenge. */
void bc_handle_gamespy(bc_socket_t *sock, const bc_addr_t *from,
                       const u8 *data, int len);

/* Find the peer that owns an object_id. Returns peer_slot or -1. */
int find_peer_by_object(i32 object_id);

/* Find the minimum efficiency among Powered ser_list entries whose children
 * include subsystems of the given type. Returns 1.0f if none found.
 * Used by the simulation tick in the main loop. */
f32 bc_powered_efficiency(const bc_ship_state_t *ship,
                          const bc_ship_class_t *cls,
                          const char *child_type);

/* Torpedo callbacks for bc_torpedo_tick() -- defined in server_dispatch.c,
 * called from the main loop's simulation tick. */
void bc_torpedo_hit_callback(int shooter_slot, i32 target_id,
                             f32 damage, f32 damage_radius,
                             bc_vec3_t impact_pos,
                             void *user_data);

bool bc_torpedo_target_pos(i32 target_id, bc_vec3_t *out_pos,
                           void *user_data);

#endif /* OPENBC_SERVER_DISPATCH_H */
