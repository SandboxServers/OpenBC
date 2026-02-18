#include "openbc/peer.h"
#include <string.h>

void bc_peers_init(bc_peer_mgr_t *mgr)
{
    memset(mgr, 0, sizeof(*mgr));
    for (int i = 0; i < BC_MAX_PLAYERS; i++) {
        mgr->peers[i].state = PEER_EMPTY;
        mgr->peers[i].object_id = -1;
        mgr->peers[i].class_index = -1;
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
            /* Zero the struct, then set fields.
             *
             * IMPORTANT: With i686-w64-mingw32-gcc -O2, the compiler
             * aggressively eliminates stores it considers dead.  When
             * memset() is followed by a write to the same memory, the
             * compiler may treat the write as redundant if it can prove
             * the value is already zero.  For the addr field specifically,
             * we use a volatile-qualified pointer to force the store. */
            memset(&mgr->peers[i], 0, sizeof(bc_peer_t));
            mgr->peers[i].state = PEER_CONNECTING;

            /* Volatile write barrier prevents the compiler from
             * eliminating this store after the memset. */
            volatile u8 *dst = (volatile u8 *)&mgr->peers[i].addr;
            const u8 *src = (const u8 *)addr;
            for (size_t b = 0; b < sizeof(bc_addr_t); b++)
                dst[b] = src[b];

            mgr->peers[i].object_id = -1;
            mgr->peers[i].class_index = -1;
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

    /* Zero the entire struct to prevent stale data (last_recv_time, reliable
     * queue, etc.) from triggering spurious timeouts if the slot is reused.
     * Use volatile to prevent the -O2 dead-store elimination bug. */
    volatile u8 *dst = (volatile u8 *)&mgr->peers[slot];
    for (size_t b = 0; b < sizeof(bc_peer_t); b++)
        dst[b] = 0;
    mgr->peers[slot].object_id = -1;
    mgr->peers[slot].class_index = -1;
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
