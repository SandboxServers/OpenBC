# TGMessage Routing — Clean Room Specification

> **Major correction 2026-05-29**: This doc previously described an "Automatic Relay (C++ Layer)" model that was factually wrong per binary RE. Relay is **per-opcode**, decided **inside each handler**, **after** the local effect. Following the old model produces duplicate event delivery for opcodes 0x06, 0x0D, 0x13, 0x14, 0x15, 0x17, 0x18, 0x29. (NOTE: 0x1A removed from this list per Pass 1, see below.) The STBC-side v5-validated cleanroom companion at `docs/networking/tgmessage-routing-cleanroom.md` in the STBC-Dedicated-Server repo has the full evidence chain.

> **Pass 1 reshape (2026-05-29)**: A subsequent binary-truth catalog of all host-side
> TGEvent emissions (`host-event-emission-catalog-20260529` STBC memo) re-shaped the
> per-opcode policy for two opcodes:
>
> - **0x1A BeamFire is NOT LOCAL-ONLY.** The stock host RELAYS 0x1A to the `Forward`
>   group from `FUN_0069FBB0`. BeamFire is exclusively client-input-originated;
>   the host's role is purely receive → relay → locally-apply. The per-opcode policy
>   table below now reflects this.
>
> - **0x29 Explosion is host-emit-only-for-catch-up.** The stock host emits 0x29 ONLY
>   from `RequestObj 0x006A02A0` (object recovery) and `NewPlayerInGame 0x006A1E70`
>   (initial-join roster sync) — never from per-tick combat. Per-tick explosion damage
>   replicates via opcode 0x1C StateUpdate.
>
> See `docs/bugs/bug-reports/20260529-per-handler-relay-cascade.md` (REVISED 2026-05-29
> section) and `docs/bugs/bug-reports/20260529-explosion-overemission.md` for the
> per-OpenBC impact.

Behavioral specification of the Bridge Commander TGMessage routing system, described purely
in terms of observable behavior. No binary addresses, decompiled code, or implementation
details. Suitable for clean-room reimplementation.

For the reverse engineering analysis with addresses and decompiled code, see
[tgmessage-routing.md](tgmessage-routing.md). The v5-validated companion document with the
full evidence chain for the per-handler relay model is
`docs/networking/tgmessage-routing-cleanroom.md` in the STBC-Dedicated-Server repo.

---

## Overview

Bridge Commander multiplayer uses a two-layer message system:

- **Transport layer**: Handles reliable delivery, fragmentation, connection management.
  Messages at this layer have a **transport type** byte.
- **Application layer**: Game-specific messages carried as opaque payloads inside transport
  messages. The first byte of the payload is the **game opcode**.

The server (host) acts as a hub in a star topology. All client-to-client communication
passes through the host.

There are **three** routing mechanisms (not two):

1. **Per-handler relay** (C++ layer, per-opcode policy via the `Forward` group)
2. **Python script messaging** (`SendTGMessage` / `SendTGMessageToGroup`)
3. **Connect-event broadcast** (transport-layer join/leave coordination)

Each is described below.

---

## Transport Layer

### Transport Message Types

The transport layer supports up to 256 message types (one byte). Seven are defined:

| Type | Purpose |
|------|---------|
| 0x00 | Game data message (carries application-layer payload) |
| 0x01 | Acknowledgement (reliable delivery tracking) |
| 0x02 | Connection request |
| 0x03 | Connection acknowledgement |
| 0x04 | Boot / forced disconnect |
| 0x05 | Graceful disconnect |
| 0x32 | General-purpose data message (with fragment support) |

All other transport types are undefined. Packets with undefined transport types are
silently dropped — no error, no crash.

### Transport Type Registration

A registration function exists in the SWIG API (`TGNetwork_RegisterMessageType`) that
allows adding custom transport types at runtime. Stock code never calls it. All game
messages use the existing type 0x00 or 0x32 transports.

### Packet Format

Each UDP packet contains:
1. One byte: sender peer ID
2. One byte: count of sub-messages
3. N sub-messages, each starting with a transport type byte

The entire packet (after byte 0) is encrypted with a stream cipher. GameSpy protocol
packets (starting with `\` / 0x5C) are never encrypted.

---

## Application Layer — Game Opcodes

### Opcode Byte

The first byte of a transport message's payload is the **game opcode**. This determines
how the rest of the payload is interpreted.

### Three C++ Dispatchers

Game opcodes are processed by three independent C++ event handlers, all triggered by the
same network message event:

| Dispatcher | Opcodes Handled |
|------------|----------------|
| MultiplayerWindow | 0x00 (Settings), 0x01 (GameInit), 0x16 (UICollision) |
| MultiplayerGame | 0x02-0x2A (game objects, events, combat, players) |
| NetFile | 0x20-0x27 (checksums, file transfer) |

Each dispatcher reads the first payload byte, checks if it matches a known opcode, and
processes it. **Unknown opcodes are silently ignored** — no error, no rejection, no log.

### Python Event Handlers

Python scripts register handlers on the same network message event. They fire for ALL
incoming messages, read the opcode byte from the payload, and compare against their own
constants. This is how messages with opcodes > 0x2A are processed.

Stock Python handles these opcodes:

| Opcode | Decimal | Name | Handler |
|--------|---------|------|---------|
| 0x2C | 44 | CHAT_MESSAGE | MultiplayerMenus.ProcessMessageHandler |
| 0x2D | 45 | TEAM_CHAT_MESSAGE | MultiplayerMenus.ProcessMessageHandler |
| 0x35 | 53 | MISSION_INIT_MESSAGE | Mission1.ProcessMessageHandler |
| 0x36 | 54 | SCORE_CHANGE_MESSAGE | Mission1.ProcessMessageHandler |
| 0x37 | 55 | SCORE_MESSAGE | Mission1.ProcessMessageHandler |
| 0x38 | 56 | END_GAME_MESSAGE | MissionShared (via EndGame) |
| 0x39 | 57 | RESTART_GAME_MESSAGE | Mission1.ProcessMessageHandler |
| 0x3F | 63 | SCORE_INIT_MESSAGE | Mission2/3/5.ProcessMessageHandler |
| 0x40 | 64 | TEAM_SCORE_MESSAGE | Mission2/3/5.ProcessMessageHandler |
| 0x41 | 65 | TEAM_MESSAGE | Mission2/3/5.ProcessMessageHandler |

### MAX_MESSAGE_TYPES

The constant `App.MAX_MESSAGE_TYPES` equals **43 (0x2B)**. It represents the count of
C++-dispatched game opcodes. Python message types are defined as offsets from this value:
```
CHAT_MESSAGE         = MAX_MESSAGE_TYPES + 1   = 44
TEAM_CHAT_MESSAGE    = MAX_MESSAGE_TYPES + 2   = 45
MISSION_INIT_MESSAGE = MAX_MESSAGE_TYPES + 10  = 53
```

This is a convention, not a technical limit. Mods can define types at any value 0-255.

---

## Message Routing

### Network Topology: Star (Hub and Spoke)

```
Client A  ←→  HOST  ←→  Client B
                ↑
Client C  ←————┘
```

- Each client maintains a single connection: to the host.
- The host maintains connections to all clients.
- There are no direct client-to-client connections.

### Sending API

Three send modes are available via Python's `SendTGMessage(target_id, message [, key])`:

1. **`target_id = 0`** — broadcast to all peers (Clone-per-peer fan-out).
2. **`target_id = N`** (positive) — unicast to specific peer (binary-searched by `peer+0x18`).
3. **`target_id = -1`** — lookup peer by the 4th argument used as a `peer+0x1C` key
   (binary-walk via the internal lookup helper). Whether stock Python ever invokes this
   mode is an open question; a clean-room server should still accept the call shape and
   handle the lookup miss by returning the standard not-found error code.

Additionally:

- **`SendTGMessageToGroup(group_name, message)`** — Sends to all members of a named group.
  The `"NoMe"` group contains all peers except the local player (the local player is
  the "Me" the group excludes).

### Broadcast Behavior by Role

| Sender | SendTGMessage(0, msg) | SendTGMessageToGroup("NoMe", msg) |
|--------|----------------------|-----------------------------------|
| Client | Goes to host only (client has 1 peer) | Goes to host only |
| Host | Goes to all clients | Goes to all clients (host excluded) |

### Per-Handler Relay (C++ Layer)

> **WARNING — This section replaces the pre-2026-05-29 "Automatic Relay (C++ Layer)" claim.**
> The pre-v5 doc claimed the C++ transport layer automatically and unconditionally relayed
> every received game message to all other clients, opaque to the opcode, before dispatch.
> **That is not how the binary works.** Relay is per-opcode, performed inside the handler
> body, after the local effect, and gated on the multiplayer + transport-up flags. OpenBC
> implementers MUST NOT implement a single transport-level relay; doing so produces
> duplicate event delivery for the local-only opcodes (0x06, 0x0D, 0x13, 0x14, 0x15, 0x17,
> 0x18). (NOTE: 0x1A and 0x29 were previously in this list but were re-classified per
> Pass 1 (2026-05-29) — 0x1A's handler DOES relay, and 0x29 is host-emit-from-catch-up;
> see the per-opcode policy table for the current classification.) See the STBC-side
> companion `docs/networking/tgmessage-routing-cleanroom.md`
> for the full v5-validated evidence chain.

**How relay actually happens.** When the host's MultiplayerGame dispatcher routes a game
opcode to its handler, the **handler decides** whether to forward the message. A relaying
handler does so explicitly inside the handler body, AFTER the local effect:

1. **Local effect first.** The handler runs its normal logic (deserialize event, post to
   the local EventManager, etc.).
2. **Clone.** The handler invokes the message Clone slot so the original can be released
   after local processing.
3. **Forward group lookup.** The handler looks up the `Forward` group by name in the
   network's group table.
4. **Send-to-group fan-out.** The handler calls the network's per-group iterate primitive
   (the `GenericEventForward` helper, binary FUN_0069FDA0), which clones-and-enqueues a
   copy on each group member's send queue.

The whole sequence is gated on `g_IsMultiplayer && TGWinsockNetwork != NULL`. In
single-player or before networking is up, the handler runs the local effect and skips the
forward.

The dispatcher has already decoded the opcode and selected a per-opcode handler **before**
relay can happen. Implementers must NOT implement transport-level relay; relay decisions
are per-handler.

**Which opcodes relay, which don't.** This is the canonical per-opcode policy table:

| Opcode | Name | Relays? | Notes |
|--------|------|---------|-------|
| 0x02 | ObjCreate | **Yes** | After local create |
| 0x03 | ObjCreateTeam | **Yes** | After local create |
| 0x04 | (dead) | **Yes** | ObjCreate-group default (jump-table); boot sent via TGBootPlayerMessage |
| 0x05 | (dead) | **Yes** | ObjCreate-group default (jump-table) |
| 0x06 | PythonEvent | **No** | **LOCAL-ONLY** per FUN_0069F880; no SendToGroup call |
| 0x07 | StartFiring | **Yes** | via `GenericEventForward` |
| 0x08 | StopFiring | **Yes** | via `GenericEventForward` |
| 0x09 | StopFiringAtTarget | **Yes** | via `GenericEventForward` |
| 0x0A | SubsysStatus | **Yes** | via `GenericEventForward` |
| 0x0B | AddToRepairList | **Yes** | via `GenericEventForward` |
| 0x0C | ClientEvent | **Yes** | via `GenericEventForward` |
| 0x0D | PythonEvent2 | **No** | **LOCAL-ONLY** — same handler as 0x06 |
| 0x0E | StartCloak | **Yes** | via `GenericEventForward` |
| 0x0F | StopCloak | **Yes** | via `GenericEventForward` |
| 0x10 | StartWarp | **Yes** | via `GenericEventForward` |
| 0x11 | RepairListPriority | **Yes** | via `GenericEventForward` |
| 0x12 | SetPhaserLevel | **Yes** | via `GenericEventForward` |
| 0x13 | HostMsg / SelfDestruct | **No** | **LOCAL-ONLY**; no SendToGroup call |
| 0x14 | DestroyObject | **No** | **LOCAL-ONLY**; no SendToGroup call |
| 0x15 | CollisionEffect | **No** | **LOCAL-ONLY** (C→S only per leaf #15); server emits 0x06 PythonEvent damage instead |
| 0x17 | DeletePlayerUI | **No** | **LOCAL-ONLY** (S→C only per leaf #17) |
| 0x18 | DeletePlayerAnim | **No** | **LOCAL-ONLY**; no SendToGroup call |
| 0x19 | TorpedoFire | **Yes** | TorpedoFireHandler — same Clone+SendToGroup pattern |
| 0x1A | BeamFire | **Yes** — host receives client input, relays to other clients | Per Pass 1 (2026-05-29): host receives via `FUN_0069FBB0`, relays to `Forward` group, then locally applies via `FUN_005762B0`. Stock host NEVER originates 0x1A from simulation. Per-tick beam damage replicates via opcode 0x1C StateUpdate. |
| 0x1B | TorpTypeChange | **Yes** | via `GenericEventForward` |
| 0x1C | StateUpdate | **Yes** (re-emitted, not raw-relayed) | Server re-emits for owned objects, not opaque-forwarded |
| 0x1D | ObjNotFound | case-by-case | Object recovery; see leaf #18 |
| 0x1E | RequestObj | case-by-case | Object recovery; see leaf #18 |
| 0x1F | EnterSet | case-by-case | Object recovery; see leaf #18 |
| 0x29 | Explosion | **Catch-up only** | Per Pass 1 (2026-05-29): host emits 0x29 ONLY from `RequestObj 0x006A02A0` (object recovery) and `NewPlayerInGame 0x006A1E70` (initial-join roster sync) — never per-tick combat. Per-tick explosion damage replicates via opcode 0x1C StateUpdate. The RECEIVE-side handler `FUN_006A0080` is LOCAL-ONLY (no SendToGroup), but the host's TRANSMIT path is non-zero, gated to those two catch-up scenarios via `DamageableObject__SendExplosions_0x29 @ FUN_00595C60`. |
| 0x2A | NewPlayerInGame | **Yes** | Triggers join handshake; replayed/forwarded as part of join flow |

**Implementation rule for OpenBC:** Implement relay **inside each handler**, after the
local effect, gated on `is_multiplayer && transport_up`. Do **NOT** implement a single
transport-level relay that fires on receive — that produces duplicate delivery on the 7
local-only opcodes listed above (0x06, 0x0D, 0x13, 0x14, 0x15, 0x17, 0x18). 0x1A relays
via its per-handler `Forward`-group call (the standard per-handler relay pattern); 0x29
is host-emit-only-from-catch-up, never relayed (see 0x29 row for emission scenarios).
The `Forward` group's member list is maintained as "all peers except the original sender",
so a single SendToGroup call produces a clean "to other clients" fan-out without manual
sender-exclusion.

### NoMe and Forward Groups: Created by C++, Not Python

The `NoMe` group (used by Python `SendTGMessageToGroup`) and the `Forward` group (used by
the per-handler C++ relay) are **both created by the C++ MultiplayerGame constructor**
(binary FUN_0069E590, with name-string xrefs at the `NoMe` literal `0x008E5528` and the
`Forward` literal `0x008D94A0`). They are **NOT** created by Python script init.

A clean-room server's equivalent server-side multiplayer-init path MUST create both
groups before any handler can call SendToGroup or before any Python script can call
SendTGMessageToGroup. Python only **uses** these groups; it does not create them.

If your server delegates group creation to Python script init, the very first handler call
(or chat message) will hit a "group not found" error and the relay will silently do
nothing — producing a server where players join but no in-game events propagate.

### Python-Level Relay (Selective)

Some messages are relayed entirely in Python on the host:

- **CHAT_MESSAGE (0x2C)**: Host's Python handler explicitly forwards via
  `SendTGMessageToGroup("NoMe", copy)`. There is **no C++ auto-relay** for chat — the C++
  dispatcher silently drops 0x2C (out-of-range), and the Python handler is the only
  forwarder.

  > **OPEN QUESTION — chat 1:2 send/receive ratio.** A 2026-02-24 audit observed
  > `0x2C CHAT_MESSAGE` at 5 client→server, 10 server→client, despite `NoMe` excluding the
  > host (so each chat should reach one OTHER client per send, producing a 1:1 ratio in a
  > 2-player session). The pre-v5 doc speculated this was a double-delivery from C++
  > auto-relay + Python NoMe; with the correction landed (no C++ auto-relay for 0x2C),
  > that hypothesis is **false** and the 1:2 ratio is genuinely unexplained. No current
  > causal model.

- **TEAM_CHAT_MESSAGE (0x2D)**: Host's Python handler selectively forwards only to
  teammates via individual `SendTGMessage(player_id, copy)` calls, one per teammate. There
  is no C++ auto-relay for 0x2D either.

### Third Routing Mechanism: Connect-Event Broadcast

The third routing mechanism is the **connect-event broadcast** path that tells existing
clients about a new peer joining (and a peer leaving). This is distinct from per-handler
game-data relay and Python script messaging: it does not forward arbitrary game data, only
connect/disconnect events.

**Connect path.** When the host receives a transport-type 0x02 connect message from a new
peer, the connect handler (binary FUN_006B63A0):

1. Parses the peer ID from the incoming TGConnectMessage.
2. Registers the peer in the network's peer array.
3. Raises event `0x60007` (`ET_NEW_PEER_CONNECTED`) on the local EventManager so server
   logic (gamemode, scoring, scoreboard) can react.
4. Calls the network's broadcast helper (binary FUN_006B51E0) to **send the connect event
   itself** to other already-connected clients. This is what makes existing clients see a
   new player appear in their peer list and scoreboard.

The connect-event broadcast is gated on the host flag (`this+0x10E`): only the host
performs this broadcast. Clients receive connect events about other peers from the host,
not from the joining peer directly.

**Disconnect path.** A symmetric disconnect handler performs the same broadcast pattern
for peer-leaving events, again gated on the host flag.

**Why this matters for a clean-room implementation.** A server that implements only
per-handler game-data relay (mechanism #1) and Python script messaging (mechanism #2) but
omits the connect-event broadcast (mechanism #3) will produce a multiplayer experience
where new players join successfully but existing clients never see them — no scoreboard
entry, no player list update, no team-roster change. The connect-event broadcast is a
required mechanism, not an optional optimization.

---

## Message Filtering

### What Gets Filtered

The server has **no message type whitelist**. The filtering that does exist is:

1. **Transport type**: Unknown transport types (unregistered factory entries) cause the
   packet to be silently dropped at the transport layer.

2. **Connection state**: Messages from disconnecting peers are not relayed.

3. **Per-handler relay policy**: Each game-opcode handler decides whether to forward; the
   9 LOCAL-ONLY opcodes never reach the `Forward` group.

4. **Python-level**: Individual Python handlers only process opcodes they recognize,
   ignoring all others.

### What Does NOT Get Filtered

- **Game opcode value at the routing primitive**: Once a handler has decided to call
  SendToGroup, the routing primitive operates on the message handle and never re-inspects
  the opcode. (Decision-to-relay happens earlier, inside the handler.)
- **Payload content**: Never examined during routing fan-out (SendToGroup operates on
  the message handle, not its bytes).
- **Message size**: Subject only to transport-layer length limits (13-bit or 14-bit
  depending on transport type, with fragmentation support for type 0x32).
- **Game opcode at the C++ dispatchers**: No bounds check, no range validation, no
  whitelist. The MultiplayerGame dispatcher's `EAX > 0x28` bias-bounds check is a
  jump-table guard, not a security filter — out-of-range opcodes silently fall through.

---

## Mod Custom Message Types

### How Mods Define Custom Types

Mods write a custom opcode byte as the first byte of a TGMessage payload:

```python
# Example: Kobayashi Maru
KM_CUSTOM_MESSAGE = 205
kStream.WriteChar(chr(KM_CUSTOM_MESSAGE))
# ... write payload data ...
pMessage.SetDataFromStream(kStream)
pNetwork.SendTGMessage(0, pMessage)    # broadcast
```

### How Custom Types Survive the Server

1. Client creates a TGMessage with a custom opcode (e.g., 205) in the payload.
2. Transport layer wraps it in a standard type-0x32 transport message.
3. Host receives the transport message and deserializes the payload opaquely (no opcode
   inspection at the transport layer).
4. Host's C++ MultiplayerGame dispatcher reads opcode 205, fails the `EAX > 0x28`
   bias-bounds check, and silently falls through to the cleanup label. No C++ relay
   occurs (custom opcodes do not have a handler, so no handler can decide to relay).
5. Host's Python `ProcessMessageHandler` on `ET_NETWORK_MESSAGE_EVENT` reads opcode 205
   from the payload — the mod's Python handler matches and processes the message.
6. If the host's Python wants to forward the custom message to other clients, it calls
   `SendTGMessageToGroup("NoMe", clone)` — Python-level relay (mechanism #2).

### Available Opcode Ranges

| Range | Used By |
|-------|---------|
| 0x00-0x2A (0-42) | C++ dispatchers (stock game opcodes) |
| 0x2C-0x2D (44-45) | Stock Python: chat messages |
| 0x2E-0x34 (46-52) | **Unused** (available for mods) |
| 0x35-0x39 (53-57) | Stock Python: scoring/game flow |
| 0x3A-0x3E (58-62) | **Unused** (available for mods) |
| 0x3F-0x41 (63-65) | Stock Python: team mode scoring |
| 0x42-0xFF (66-255) | **Unused** (available for mods) |

Mods can also reuse stock Python opcodes by replacing the Python handlers.

### Known Mod Allocations

| Mod | Types | Decimal |
|-----|-------|---------|
| Stock team modes | MAX_MESSAGE_TYPES + 20-22 | 63-65 |
| Kobayashi Maru | hardcoded | 205, 211-214 |
| BC Remastered | MAX_MESSAGE_TYPES + 10-14 | 53-57 (replaces stock handlers) |

---

## Behavioral Guarantees

For a clean-room reimplementation, the following behaviors must be preserved:

1. **The host MUST relay each game message according to its per-opcode relay policy.**
   Most game opcodes (movement, weapon fire, generic event forwards, BeamFire 0x1A) relay
   via the per-handler `GenericEventForward` (or `Forward`-group) helper. PythonEvent
   opcodes (0x06, 0x0D) and several others (0x13, 0x14, 0x15, 0x17, 0x18) are LOCAL-ONLY
   at the handler. 0x29 Explosion has a non-zero host emit path but it's gated to
   catch-up scenarios only (RequestObj + NewPlayerInGame response paths). See the
   per-opcode policy table for the full classification. Following the pre-v5
   transport-level relay model causes
   duplicate event delivery and is the documented OpenBC parity bug.

2. **The game opcode byte MUST NOT be examined during the routing fan-out itself.** The
   SendToGroup primitive operates on the message handle and clones-and-enqueues a copy
   per recipient without inspecting the payload. (Decision-to-relay happens earlier,
   inside the handler; once the handler has decided to call SendToGroup, the routing
   primitive is opcode-agnostic.)

3. **Unknown game opcodes MUST be silently ignored** by C++ dispatchers. No error logging,
   no disconnection, no rejection.

4. **Python event handlers MUST fire for all incoming messages**, not just those with
   known opcodes. This allows mods to register handlers for custom types.

5. **The "NoMe" and "Forward" groups MUST be routing-only** — they select recipients, they
   do not filter or validate message content. Both MUST be created by the server-side
   multiplayer-init path (equivalent of the C++ MultiplayerGame ctor), NOT by Python
   script init.

6. **SendTGMessage(0, msg) from a client MUST reach the host**, which then either relays
   per-handler (for game opcodes whose handler chooses to relay) or forwards via Python
   (for opcodes the Python handler chooses to forward). This is the standard mod
   broadcasting pattern.

7. **No maximum message type enforcement beyond byte width** (0-255).

8. **The host MUST broadcast connect and disconnect events to existing clients** so other
   peers learn about joins and leaves. This is mechanism #3 (connect-event broadcast) and
   is distinct from per-handler game-data relay; omitting it produces a server where
   players join but nobody sees them.

---

## PythonEvent Dispatch: 0x06 vs 0x0D

A critical routing distinction: PythonEvent (0x06) and PythonEvent2 (0x0D) share the same
handler on the receiving side, and BOTH are LOCAL-ONLY.

### Both Are LOCAL-ONLY (No Handler Relay)

When either 0x06 or 0x0D arrives at a peer, the handler deserializes the event and posts it
to the local event manager. Neither opcode triggers a relay — the handler does not forward
the message to other peers. Verified by spot-checking the handler at binary FUN_0069F880:
**zero SendToGroup, zero TGWinsockNetwork_SendTGMessage, zero Clone calls.**

### How Clients Receive PythonEvents

Clients do NOT receive relayed copies of 0x06 or 0x0D. Instead, the server **generates
fresh 0x06 messages** from its own simulation:

1. The server's repair system fires repair completion events locally
2. A server-side event handler catches these events (3 specific event types)
3. The handler constructs a NEW opcode 0x06 message and sends it to all clients

Similarly, when a ship explodes on the server:
1. The server's death handler fires an object-exploding event
2. A server-side event handler catches it
3. Constructs a NEW opcode 0x06 message and sends to all clients

These are **freshly constructed messages**, not relays of anything a client sent.

### PythonEvent2 (0x0D) Is Client-to-Server Only

Opcode 0x0D flows exclusively C→S. All 75 observed instances in a 33.5-minute combat
session carried event code 0x010C (object pointer events — weapon/phaser/tractor target
notifications). The server processes them locally and does NOT forward them.

**Evidence**: 0x0D shows a 1:1 ratio of wire packets to unique events (75 wire, 75 events).
Compare with relayed opcodes like StartFiring (0x07) which show a 3:1 ratio in a 3-player
game (2,918 wire, 978 unique events).

### Implementation Rule

- **0x06**: Server generates from its own simulation → sends to clients. Never relayed.
- **0x0D**: Client sends to server. Server processes locally. **Never forwarded to other clients.**
- **Relaying 0x0D is WRONG**: It causes duplicate events on receiving clients (the relay
  copy plus the server's independently-generated 0x06 response).

---

## Implementation Considerations for Dedicated Server

A headless dedicated server reimplementation must:

1. **Implement per-opcode relay inside each handler, after the local effect.** Do NOT
   implement a single transport-level relay; that produces duplicate delivery on the 7
   local-only opcodes (0x06 PythonEvent, 0x0D PythonEvent2, 0x13 HostMsg, 0x14
   DestroyObject, 0x15 CollisionEffect, 0x17 DeletePlayerUI, 0x18 DeletePlayerAnim).
   0x1A BeamFire DOES relay via its per-handler `Forward`-group call (the standard
   per-handler relay pattern); 0x29 Explosion is host-emit-only-from-catch-up — gated to
   `RequestObj` and `NewPlayerInGame` response paths via
   `DamageableObject__SendExplosions_0x29 @ FUN_00595C60`, never per-tick combat. The
   `Forward`-group fan-out is the canonical mechanism for relaying handlers; replicate
   the `Clone -> SendToGroup("Forward")` sequence per
   handler that should forward.

2. **Create the `NoMe` and `Forward` groups during server-side multiplayer initialization**
   — NOT in Python script init. The C++ MultiplayerGame ctor (binary FUN_0069E590) is what
   creates the groups in stock BC; a clean-room server's equivalent server-init path must
   register both groups against the network's group table before any handler can call
   SendToGroup or before any Python script can call SendTGMessageToGroup.

3. **Broadcast connect and disconnect events to existing clients on the host side.** When
   a new peer's transport-type-0x02 connect message arrives, register the peer, post the
   `ET_NEW_PEER_CONNECTED` event locally, AND send a copy of the connect event to all
   other already-connected peers. Apply the symmetric pattern for disconnect. This is
   mechanism #3 (connect-event broadcast) and is required, not optional.

4. **Not add filtering based on game opcode**. Even if the server doesn't understand a
   custom mod message type, the C++ dispatcher must silently fall through and the Python
   layer must still receive the event.

5. **Handle Python-level messages** (chat, scoring) if the server needs to participate in
   game logic (e.g., computing scores, managing game state). Chat relay specifically MUST
   happen at the Python layer; there is no C++ auto-relay for 0x2C / 0x2D.

6. **Preserve the star topology** — clients expect to send only to the host, and expect
   the host to relay (or not) to all other clients per the policy table.

7. **Not crash or disconnect clients** for sending unrecognized message types. Silent
   ignore is the correct behavior at every layer.

8. **Support all three SendTGMessage modes** (`target_id = 0`, `target_id = N`,
   `target_id = -1`). The `target_id = -1` mode looks up the peer by the 4th argument used
   as a `peer+0x1C` key. Whether stock Python ever invokes this mode is an open question;
   a clean-room server should still accept the call shape and handle the lookup miss by
   returning the standard not-found error code.
