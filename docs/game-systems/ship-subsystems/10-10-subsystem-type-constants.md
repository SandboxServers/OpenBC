# 10. Subsystem Type Constants


These constants identify subsystem types in the scripting API and are used during ship setup:

### System-Level Types (remain in serialization list)

| Constant | Name |
|----------|------|
| `CT_SHIP_SUBSYSTEM` | Base subsystem type |
| `CT_POWERED_SUBSYSTEM` | Base powered subsystem |
| `CT_WEAPON_SYSTEM` | Generic weapon system container |
| `CT_TORPEDO_SYSTEM` | Torpedo system container |
| `CT_PHASER_SYSTEM` | Phaser system container |
| `CT_PULSE_WEAPON_SYSTEM` | Pulse weapon system container |
| `CT_TRACTOR_BEAM_SYSTEM` | Tractor beam system container |
| `CT_POWER_SUBSYSTEM` | Reactor / warp core |
| `CT_SENSOR_SUBSYSTEM` | Sensor array |
| `CT_CLOAKING_SUBSYSTEM` | Cloaking device |
| `CT_WARP_ENGINE_SUBSYSTEM` | Warp engine system |
| `CT_IMPULSE_ENGINE_SUBSYSTEM` | Impulse engine system |
| `CT_HULL_SUBSYSTEM` | Hull section |
| `CT_SHIELD_SUBSYSTEM` | Shield generator |
| `CT_REPAIR_SUBSYSTEM` | Repair system |

### Child Types (removed from list, attached to parents)

| Constant | Name | Parent |
|----------|------|--------|
| `CT_PHASER_BANK` | Individual phaser bank | Phaser System |
| `CT_TORPEDO_TUBE` | Individual torpedo tube | Torpedo System |
| `CT_TRACTOR_BEAM_PROJECTOR` | Individual tractor projector | Tractor Beam System |
| `CT_PULSE_WEAPON` | Individual pulse weapon | Pulse Weapon System |
| `CT_ENGINE_PROPERTY` | Individual engine | Impulse or Warp Engine System |

---

