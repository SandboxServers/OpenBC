# Collision Test (2026-02-22)


**Source**: Side-by-side packet traces — stock dedi vs OpenBC. Initial test: Sovereign,
environment collision, death, respawn, second collision. Follow-up (same date): 4m 24s
session with 1 player cycling through 4 of 16 flyable ships (Sovereign, Galaxy, Ambassador,
Warbird) across 3 species (Federation, Federation, Romulan).

### Per-Opcode Match Status

| Opcode | Name | Status | Notes |
|--------|------|--------|-------|
| Transport | Header, ACK, reliable/unreliable | **MATCH** | Byte-for-byte identical |
| 0x03 | ConnectAck | **MATCH** | Slot assignment correct |
| 0x20-0x27 | Checksum exchange | **MATCH** | All 5 rounds |
| 0x28 | ChecksumComplete | **MATCH** | 1 byte, no payload |
| 0x00 | Settings | **MATCH** | Field order, bit byte (0x61), map, checksum |
| 0x01 | GameInit | **MATCH** | Single byte |
| 0x03 | ObjCreateTeam | **MATCH** | 118-byte payload byte-for-byte identical |
| 0x15 | CollisionEffect | **MATCH** | Factory, event code, contacts, force float |
| 0x1C | StateUpdate | **MATCH** | Structure correct (flags, CF16, CompressedVector) |
| 0x06 (0x8129) | ObjectExplodingEvent | **MATCH** | Factory, event, fields identical (lifetime differs) |
| 0x06 (0x0101) | TGSubsystemEvent | **MATCH** (structure) | Wire format correct BUT object IDs from wrong range |
| 0x13 | HostMsg (self-destruct) | **MATCH** | 1-byte opcode, no payload, triggers death chain |
| 0x36 | ScoreChange | **MATCH** | `36 00 00 00 00 02 00 00 00 01 00 00 00 00` identical |
| 0x03 | ObjCreateTeam (multi-species) | **MATCH** | 4 species tested (2,3,5,8) — all create correctly |
| 0x35 | MissionInit | **MISMATCH** | byte[0]: stock=0x08, OpenBC=0x07 (off-by-one) |
| 0x17 | DeletePlayerUI | **MISSING** | Stock sends at join, OpenBC doesn't |
| 0x29 | Explosion | ~~SPURIOUS~~ **FIXED** | #60: no longer sent for collision kills |

### Behavioral Gaps (separate issues filed)

| # | Gap | Severity | Issue | Status |
|---|-----|----------|-------|--------|
| 1 | Post-respawn collision ownership stale | HIGH | #57 | Open |
| 2 | ~~Subsystem object IDs from wrong range~~ | ~~HIGH~~ | #58 | **FIXED** |
| 3 | Missing DeletePlayerUI (0x17) at join | MEDIUM | #59 | Open |
| 4 | ~~Spurious Explosion (0x29) on collision kill~~ | ~~MEDIUM~~ | #60 | **FIXED** |
| 5 | Too few SubsystemDamage events at death | MEDIUM | #61 | Open (per-ship data on #63) |
| 6 | MissionInit maxPlayers 7 vs 8 | LOW | #62 | Open |
| 7 | ~~Combat death auto-respawn~~ | ~~HIGH~~ | #38 | **FIXED** |

See [20260222-collision-test-parity-gaps.md](../../bugs/bug-reports/20260222-collision-test-parity-gaps.md)
for full details with hex dumps and server log excerpts.

### Per-Ship Subsystem Event Breakdown (4 of 16 ships tested)

| Species | Ship | Death | TGSubsystemEvents | Health Burst | Repair UI |
|---------|------|-------|-------------------|--------------|-----------|
| 5 | Sovereign | Collision | 1 / ~13 | No | — |
| 3 | Galaxy | Collision | 9 / ~13 | No | — |
| 2 | Ambassador | Self-destruct | 5 / ~13 | Yes (3 pkts) | — |
| 8 | Warbird | Self-destruct | 6 / ~13 | Yes (3 pkts) | **Yes** |

- **First-ship deficit**: Sovereign (1st object created) has 1 event vs 5-9 for later ships
- **Health burst**: Self-destruct deaths send 3 StateUpdate flag=0x20 packets; collision kills do not
- **End-to-end verified**: Warbird F5 repair UI showed damaged subsystems on client

Per-ship tracking comments maintained on #63.

### Not Yet Tested

- Weapon kill death sequence (beam/torpedo)
- Multi-player collision (2 ships)
- Cloak/Warp/Repair events in combat
- Chat messages during gameplay
- EndGame/RestartGame flow

---

