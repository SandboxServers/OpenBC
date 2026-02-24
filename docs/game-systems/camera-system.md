# Camera System Specification

This document describes Bridge Commander's camera mode system. Relevant to Quick Battle and single-player client rendering.

**Clean room statement**: This document describes camera behavior as observable in-game and from shipped Python scripts (`CameraModes.py`, `Camera.py`). No binary addresses, memory offsets, or decompiled code are referenced.

---

## 1. Architecture

The camera system uses a **stack-based mode model**. Each camera maintains a stack of camera modes. The active mode (top of stack) controls camera position and orientation. Modes can be pushed, popped, and replaced.

### Mode Stack Operations

| Operation | Behavior |
|-----------|----------|
| Push mode | Add new mode to top of stack (becomes active) |
| Pop mode | Remove named mode from stack (previous mode becomes active) |
| Replace | Pop all modes, push new one |

### Mode Factory

All modes are created via `CameraMode_Create(typeName, pCamera)`. The type name selects the C++ class that implements the mode's update logic. Mode attributes are then configured via `SetAttrFloat` / `SetAttrPoint`.

---

## 2. Camera Mode Types

### Chase

Follows a target from behind. The camera tracks behind the target ship, smoothly adjusting to heading changes.

**Type name**: `"Chase"`

| Attribute | Default | Description |
|-----------|---------|-------------|
| `SweepTime` | 1.0 | Smooth transition time (seconds) |
| `PositionThreshold` | 0.01 | Movement snap threshold |
| `DotThreshold` | 0.98 | Rotation snap threshold (dot product) |
| `MinimumDistance` | 2.0 | Closest zoom distance |
| `Distance` | 4.0 | Default follow distance |
| `MaximumDistance` | 40.0 | Farthest zoom distance |
| `ViewTargetOffset` | (0, 0, 0.1) | Offset of look-at point on target |
| `DefaultPosition` | (0, -1.0, 0.1) | Default camera position relative to target (behind and slightly above) |
| `MaxLagDist` | 2.0 | Maximum camera lag distance |

**Variants**:
- **Chase** — Standard rear chase camera
- **ReverseChase** — Front-facing chase (DefaultPosition Y = +1.0)

### Target

Third-person target camera. Positions camera to keep both the player ship and targeted enemy in frame.

**Type name**: `"Target"`

| Attribute | Default | Description |
|-----------|---------|-------------|
| `SweepTime` | 1.0 | Smooth transition time |
| `MinimumDistance` | 2.0 | Min zoom |
| `Distance` | 4.0 | Default distance |
| `MaximumDistance` | 40.0 | Max zoom |
| `BackWatchPos` | 7.95 | Rear offset for positioning |
| `UpWatchPos` | 0.95 | Vertical offset |
| `LookBetween` | 0.05 | Blend factor: 0.0 = look at player, 1.0 = look at target |
| `MaxLagDist` | 1.0 | Camera lag limit |
| `MaxUpAngleChange` | PI/2 | Maximum vertical rotation |

**Variants**:
- **Target** — Standard target cam
- **WideTarget** — Zoomed out (Min 8.0, Distance 32.0, Max 64.0)
- **CinematicReverseTarget** — Tighter distances (Max 16.0)

### ZoomTarget

Close-up target camera. Zooms in on the targeted ship.

**Type name**: `"ZoomTarget"`

| Attribute | Default | Description |
|-----------|---------|-------------|
| `MinimumDistance` | 4.0 | Min zoom |
| `Distance` | 4.0 | Default distance |
| `MaximumDistance` | 20.0 | Max zoom |
| `MaxLagDist` | 1.0 | Camera lag limit |

**Variants**:
- **ZoomTarget** — Standard zoom
- **ViewscreenZoomTarget** — Wider range (Min 2.0, Distance 8.0, Max 32.0)

### Map

Top-down tactical map camera. Zoomed way out to show the full battlefield.

**Type name**: `"Map"`

| Attribute | Default | Description |
|-----------|---------|-------------|
| `SweepTime` | 0.0 | Instant snap (no smooth transition) |
| `MinimumDistance` | 400.0 | Min zoom (very far out) |
| `Distance` | 1000.0 | Default distance |
| `MaximumDistance` | 10000.0 | Max zoom |

**Variants**:
- **Map** — Full tactical view
- **FreeOrbit** — Closer orbital camera (Min 30, Distance 75, Max 125)

### Locked

Fixed position and orientation relative to the parent ship. Used for bridge viewscreen cameras.

**Type name**: `"Locked"`

| Attribute | Type | Description |
|-----------|------|-------------|
| `Position` | Point3 | Camera position in ship-local coordinates |
| `Forward` | Point3 | Look direction vector |
| `Up` | Point3 | Up direction vector |

**Variants**: 8 viewscreen cameras (Forward, Left, Right, Back, Up, Down, Locked, FirstPerson), each with different Position/Forward/Up vectors.

### DropAndWatch

Cinematic camera. Drops behind the ship and watches it fly past, then slowly rotates.

**Type name**: `"DropAndWatch"`

| Attribute | Default | Description |
|-----------|---------|-------------|
| `AwayDistance` | 0.0 | Initial distance from ship |
| `RotateSpeed` | 0.0 | Initial rotation speed |
| `AnticipationTime` | 2.5 | Look-ahead time for target position |
| `ForwardOffset` | 0.5 | Forward placement offset |
| `SideOffset` | 3.0 | Side placement offset |
| `AwayDistanceFactor` | 1.2 | Distance growth factor |
| `AxisAvoidAngles` | 45.0 | Avoid axis-aligned camera angles |
| `SlowSpeedThreshold` | 0.5 | Speed threshold for slow-mode |
| `SlowRotationThreshold` | 0.1 | Rotation threshold for slow-mode |
| `RotateSpeedAccel` | 0.025 | Rotation acceleration |
| `MaxRotateSpeed` | 0.2 | Maximum rotation speed |

### TorpCam

Torpedo tracking camera. Follows a launched torpedo from behind, tracking its flight to impact.

**Type name**: `"TorpCam"`

| Attribute | Default | Description |
|-----------|---------|-------------|
| `SweepTime` | 2.0 | Smooth transition time |
| `DelayAfterTorpGone` | 2.0 | Seconds to linger after torpedo impact/miss |
| `StartDistance` | 4.0 | Initial follow distance |
| `LaterDistance` | 8.0 | Follow distance after ramp time |
| `MoveDistanceTime` | 6.0 | Time to transition from start to later distance |

### PlacementWatch

Editor camera. Follows a path defined by waypoints (used by the in-game placement editor).

**Type name**: `"PlacementWatch"`

| Attribute | Default | Description |
|-----------|---------|-------------|
| `PathSpeedScale` | 0.2 | Speed along the waypoint path |
| `TimeAlongPath` | 0.0 | Current position on path (0.0 = start) |

### PlaceByDirection

Directional placement camera. Used for bridge captain perspective.

**Type name**: `"PlaceByDirection"`

| Attribute | Type | Description |
|-----------|------|-------------|
| `StartMoveAngle` | float | Angle to start camera movement |
| `EndMoveAngle` | float | Angle to end camera movement |
| `BasePosition` | Point3 | Base camera position |
| `Movement` | Point3 | Camera movement vector |

---

## 3. Camera Mode Usage by Game Context

| Context | Camera Modes Used |
|---------|------------------|
| **Tactical combat** | Chase, Target, ZoomTarget, TorpCam |
| **Ship overview** | ReverseChase, WideTarget |
| **Tactical map** | Map, FreeOrbit |
| **Bridge viewscreen** | Locked variants (Forward, Left, Right, Back, Up, Down) |
| **Cinematics** | DropAndWatch, CinematicReverseTarget |
| **Editor** | PlacementWatch |
| **Bridge interior** | PlaceByDirection (GalaxyBridgeCaptain) |

### Quick Battle Mode Cameras

Quick Battle primarily uses:
1. **Chase** — Default third-person follow camera
2. **Target** — Combat camera (shows both player and target)
3. **ZoomTarget** — Close-up of target
4. **Map** — Tactical overview
5. **TorpCam** — Torpedo follow camera (auto-activated on torpedo fire, if enabled in settings)

---

## 4. Common Attributes

Most camera modes share these attributes:

| Attribute | Description |
|-----------|-------------|
| `SweepTime` | Duration of smooth camera transition when mode activates (0 = instant) |
| `PositionThreshold` | Distance below which camera snaps to target position (prevents jitter) |
| `DotThreshold` | Dot product above which camera snaps to target orientation |
| `MinimumDistance` | Closest allowed zoom level (scroll wheel in) |
| `Distance` | Default zoom level |
| `MaximumDistance` | Farthest allowed zoom level (scroll wheel out) |

---

## Related Documents

- **[../architecture/engine-reference.md](../architecture/engine-reference.md)** — Engine layer overview including scene graph
- **[ai-system.md](ai-system.md)** — AI system (camera follows AI-controlled ships)
