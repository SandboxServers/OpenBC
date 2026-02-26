# 13. Confidence Annotations


Every data point in this document is sourced from readable Python scripts and protocol captures. Confidence levels:

### Verified (high confidence)

| Data | Source File |
|------|-------------|
| Species IDs (Section 1) | `ships/Hardpoints/*.py` SetSpecies() calls |
| kSpeciesTuple structure (Section 2) | `Multiplayer/SpeciesToShip.py` |
| All modifier classes = 1 (Section 2.4) | `Multiplayer/SpeciesToShip.py` |
| Modifier table (Section 3) | `Multiplayer/Modifier.py` |
| Map list (Section 4) | `Multiplayer/SpeciesToSystem.py` |
| Checksum directories (Section 5.2) | `wire-format-spec.md` + protocol traces |
| StringHash algorithm (Section 5.3) | Algorithm verified via hash manifest tool output against known inputs |
| FileHash algorithm (Section 5.3) | Algorithm verified via hash manifest tool output against known inputs |
| Version string + hash (Section 5.4) | Extracted via hash manifest tool + verified in protocol |
| Impulse engine parameters (Section 9.2) | `ships/Hardpoints/*.py` ImpulseEngineProperty calls |
| Mass/inertia for 12 ships (Section 9.3) | `GlobalPropertyTemplates.py` |
| Settings packet toggles (Section 6.2) | Behavioral analysis of Settings packet in protocol captures |

### Needs Further Extraction

| Data | What's Missing | Where to Get It |
|------|---------------|-----------------|
| Map display names | Not in SpeciesToSystem.py | Each `Systems/Multi*/Multi*.py` script |
| Map spawn points | Coordinate data | Each map script's SetTranslate() calls |
| Ship mass for ~20 ships | Not in GlobalPropertyTemplates | Engine defaults in Appc.pyd or hardpoints |
| Ship subsystem counts | Per-ship subsystem list | Each hardpoint script's subsystem creation calls |
| Settings packet full layout | Complete byte-level format | Decompiled MultiplayerWindow handler |
| ObjCreateTeam full layout | Packet byte map | `objcreate-team-format.md` (partially documented) |
| Hull HP for most ships | Only Galaxy (15000) and BoP (4000) extracted | Each hardpoint script's HullProperty calls |
| Weapon parameters | Only partial (Sovereign phasers, Galaxy torpedoes, BoP disruptors) | Each hardpoint script's weapon property calls |
| Game mode constants | Frag limits, time limits from Mission*.py | `Multiplayer/Episode/Mission*/Mission*.py` |
