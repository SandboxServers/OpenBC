# 1. Subsystem Architecture: Hierarchical, Not Flat


Ship subsystems use a **two-level hierarchy**: top-level "system" containers hold individual "child" subsystems. The wire protocol serializes this hierarchy recursively, not as a flat array.

### Top-Level Systems (in the serialization list)

These are the subsystem categories that appear in the ship's serialization list. The **order** they appear is determined by the ship's hardpoint script (the order `AddToSet()` is called in `LoadPropertySet()`).

| Type Constant | Subsystem | WriteState Format | Notes |
|--------------|-----------|-------------------|-------|
| `CT_HULL_SUBSYSTEM` | Hull | Base | One or more per ship (hull + bridge) |
| `CT_SHIELD_SUBSYSTEM` | Shield Generator | Base | Shield HP sent separately via flag 0x40 |
| `CT_SENSOR_SUBSYSTEM` | Sensor Array | Powered | |
| `CT_POWER_SUBSYSTEM` | Warp Core / Reactor | Power | Writes 2 extra power-level bytes |
| `CT_IMPULSE_ENGINE_SUBSYSTEM` | Impulse Engine System | Powered | Children: individual engines |
| `CT_WARP_ENGINE_SUBSYSTEM` | Warp Engine System | Powered | Children: individual engines |
| `CT_TORPEDO_SYSTEM` | Torpedo System | Powered | Children: torpedo tubes |
| `CT_PHASER_SYSTEM` | Phaser System | Powered | Children: phaser banks |
| `CT_TRACTOR_BEAM_SYSTEM` | Tractor Beam System | Powered | Children: tractor projectors |
| `CT_CLOAKING_SUBSYSTEM` | Cloaking Device | Powered | Only on ships with cloak |
| `CT_REPAIR_SUBSYSTEM` | Repair System | Powered | |

### Child Subsystems (serialized recursively within parents)

These subsystems are **not** directly in the serialization list. They are children of their parent system and are written recursively when their parent's `WriteState` runs.

| Type Constant | Subsystem | Parent System |
|--------------|-----------|---------------|
| `CT_PHASER_BANK` | Phaser Bank | Phaser System |
| `CT_TORPEDO_TUBE` | Torpedo Tube | Torpedo System |
| `CT_TRACTOR_BEAM_PROJECTOR` | Tractor Beam Projector | Tractor Beam System |
| `CT_PULSE_WEAPON` | Pulse Weapon | Pulse Weapon System |
| `CT_ENGINE_PROPERTY` | Individual Engine | Impulse Engine or Warp Engine System |

### How Reparenting Works

When a ship is created:
1. The hardpoint script adds ALL subsystems (both systems and individual components) to the serialization list via `AddToSet()`
2. A linking pass runs that identifies child subsystems and **removes them** from the serialization list
3. Removed children are attached to their parent system's child array
4. After linking, only top-level systems remain in the serialization list

This means the serialization list has far fewer entries than the total subsystem count (e.g., Sovereign has 11 top-level entries, not 33).

---

