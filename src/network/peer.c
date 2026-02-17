#include "openbc/peer.h"
#include <string.h>

void bc_peers_init(bc_peer_mgr_t *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
    for (int i = 0; i < BC_MAX_PLAYERS; i++) {
        mgr->peers[i].state = PEER_EMPTY;
        mgr->peers[i].object_id = -1;
    }
    mgr->count = 0;
}

int bc_peers_find(const bc_peer_mgr_t *mgr, const bc_addr_t *addr)
{
    for (int i = 0; i < BC_MAX_PLAYERS; i++) {
        if (mgr->peers[i].state != PEER_EMPTY &&
            bc_addr_equal(&mgr->peers[i].addr, addr)) {
            return i;
        }
    }
    return -1;
}

int bc_peers_add(bc_peer_mgr_t *mgr, const bc_addr_t *addr)
{
    /* Find first empty slot */
    for (int i = 0; i < BC_MAX_PLAYERS; i++) {
        if (mgr->peers[i].state == PEER_EMPTY) {
            memset(&mgr->peers[i], 0, sizeof(bc_peer_t));
            mgr->peers[i].state = PEER_CONNECTING;
            /* Use memcpy instead of struct assignment: with -O2, mingw gcc
             * can optimize away `peers[i].addr = *addr` after memset,
             * leaving the address zeroed.  memcpy is a function call barrier
             * that the optimizer cannot eliminate. */
            memcpy(&mgr->peers[i].addr, addr, sizeof(bc_addr_t));
            mgr->peers[i].object_id = -1;
            bc_outbox_init(&mgr->peers[i].outbox);
            mgr->count++;
            return i;
        }
    }
    return -1;  /* All slots full */
}

void bc_peers_remove(bc_peer_mgr_t *mgr, int slot)
{
    if (slot < 0 || slot >= BC_MAX_PLAYERS) return;
    if (mgr->peers[slot].state == PEER_EMPTY) return;

    mgr->peers[slot].state = PEER_EMPTY;
    mgr->count--;
}

int bc_peers_timeout(bc_peer_mgr_t *mgr, u32 now_ms, u32 timeout_ms)
{
    int removed = 0;
    for (int i = 0; i < BC_MAX_PLAYERS; i++) {
        if (mgr->peers[i].state == PEER_EMPTY) continue;
        if (now_ms - mgr->peers[i].last_recv_time > timeout_ms) {
            bc_peers_remove(mgr, i);
            removed++;
        }
    }
    return removed;
}
