#!/usr/bin/env python3
"""
Add serialization_list and power parameters to a monolith registry JSON.

Data sources:
- docs/ship-subsystems.md Section 7 (per-ship serialization lists)
- docs/power-system.md (power parameters table)

Children that exist in the flat subsystem array get their max_condition from
there (loader ignores JSON value). Children NOT in the flat array (engine
children) use the max_condition from this JSON data.
"""
import json
import sys

# Power parameters: (power_output, main_battery_limit, backup_battery_limit,
#                     main_conduit_capacity, backup_conduit_capacity)
POWER_PARAMS = {
    "Akira":        (800, 150000, 50000, 900, 100),
    "Ambassador":   (600, 200000, 50000, 700, 100),
    "Galaxy":       (1000, 250000, 80000, 1200, 200),
    "Nebula":       (800, 100000, 150000, 1000, 200),
    "Sovereign":    (1200, 200000, 100000, 1450, 250),
    "BirdOfPrey":   (400, 80000, 40000, 470, 70),
    "Vorcha":       (800, 100000, 100000, 900, 200),
    "Warbird":      (1500, 100000, 200000, 1700, 300),
    "Marauder":     (700, 140000, 100000, 900, 200),
    "Galor":        (500, 120000, 50000, 550, 150),
    "Keldon":       (600, 140000, 50000, 700, 100),
    "CardHybrid":   (1000, 160000, 50000, 1100, 100),
    "KessokHeavy":  (1400, 100000, 100000, 1500, 100),
    "KessokLight":  (900, 120000, 80000, 1000, 50),
    "Shuttle":      (100, 20000, 10000, 140, 40),
    "CardFreighter":(400, 50000, 10000, 400, 200),
}

def sl(fmt, name, mc, power=0, children=None):
    """Build a serialization list entry."""
    e = {"format": fmt, "name": name, "max_condition": mc}
    if fmt == "powered":
        e["normal_power"] = power
    if children:
        e["children"] = children
    return e

def ch(name, mc=None):
    """Build a child reference. mc is needed for children not in the flat array."""
    c = {"name": name}
    if mc is not None:
        c["max_condition"] = float(mc)
    return c

# Serialization lists from docs/ship-subsystems.md Section 7.
# Child names MUST match the flat subsystem array names exactly (for weapon/tractor children).
# Engine children (not in flat array) get max_condition here.

SER_LISTS = {
    # === Federation ===
    "Akira": [
        sl("base", "Hull", 9000),
        sl("base", "Shield Generator", 9000),
        sl("powered", "Sensor Array", 6000, 150),
        sl("power", "Warp Core", 6000),
        sl("powered", "Impulse Engines", 3000, 50, [
            ch("Port Impulse", 3000), ch("Starboard Impulse", 3000)]),
        sl("powered", "Phasers", 8000, 200, [
            ch("Ventral Phaser 1"), ch("Ventral Phaser 2"),
            ch("Ventral Phaser 3"), ch("Ventral Phaser 4"),
            ch("Dorsal Phaser 1"), ch("Dorsal Phaser 2"),
            ch("Dorsal Phaser 3"), ch("Dorsal Phaser 4")]),
        sl("powered", "Warp Engines", 7000, 0, [
            ch("Port Warp", 3500), ch("Starboard Warp", 3500)]),
        sl("powered", "Torpedoes", 5000, 100, [
            ch("Forward Torp 1"), ch("Forward Torp 2"),
            ch("Forward Torp 3"), ch("Forward Torp 4"),
            ch("Aft Torp 1"), ch("Aft Torp 2")]),
        sl("powered", "Engineering", 7000, 1),
        sl("powered", "Tractors", 3000, 600, [
            ch("Forward Tractor"), ch("Aft Tractor")]),
        sl("base", "Bridge", 9000),
    ],
    "Ambassador": [
        sl("base", "Hull", 13000),
        sl("base", "Shield Generator", 10000),
        sl("powered", "Sensor Array", 6000, 50),
        sl("power", "Warp Core", 6000),
        sl("powered", "Impulse Engines", 3000, 100, [
            ch("Port Impulse", 3000), ch("Starboard Impulse", 3000)]),
        sl("powered", "Phasers", 8000, 150, [
            ch("Ventral Phaser 1"), ch("Ventral Phaser 2"),
            ch("Ventral Phaser 3"),
            ch("Dorsal Phaser 1"), ch("Dorsal Phaser 2"),
            ch("Dorsal Phaser 3"),
            ch("Aft Phaser 1"), ch("Aft Phaser 2")]),
        sl("powered", "Warp Engines", 8000, 0, [
            ch("Port Warp", 4000), ch("Starboard Warp", 4000)]),
        sl("powered", "Torpedoes", 5000, 100, [
            ch("Forward Torpedo 1"), ch("Forward Torpedo 2"),
            ch("Aft Torpedo 1"), ch("Aft Torpedo 2")]),
        sl("powered", "Engineering", 7000, 1),
        sl("base", "Bridge", 9000),
        sl("powered", "Tractors", 3000, 600, [
            ch("Forward Tractor"), ch("Aft Tractor")]),
    ],
    "Galaxy": [
        sl("base", "Hull", 12000),
        sl("power", "Warp Core", 7000),
        sl("base", "Shield Generator", 10000),
        sl("powered", "Sensor Array", 8000, 100),
        sl("powered", "Torpedoes", 6000, 100, [
            ch("Forward Torpedo 1"), ch("Forward Torpedo 2"),
            ch("Forward Torpedo 3"), ch("Forward Torpedo 4"),
            ch("Aft Torpedo 1"), ch("Aft Torpedo 2")]),
        sl("powered", "Phasers", 8000, 300, [
            ch("Ventral Phaser 3"), ch("Dorsal Phaser 2"),
            ch("Dorsal Phaser 4"), ch("Dorsal Phaser 1"),
            ch("Dorsal Phaser 3"), ch("Ventral Phaser 1"),
            ch("Ventral Phaser 4"), ch("Ventral Phaser 2")]),
        sl("powered", "Impulse Engines", 3000, 150, [
            ch("Port Impulse", 3000), ch("Starboard Impulse", 3000),
            ch("Center Impulse", 3000)]),
        sl("powered", "Warp Engines", 8000, 0, [
            ch("Port Warp", 4000), ch("Starboard Warp", 4000)]),
        sl("powered", "Tractors", 3000, 600, [
            ch("Aft Tractor 1"), ch("Aft Tractor 2"),
            ch("Forward Tractor 1"), ch("Forward Tractor 2")]),
        sl("base", "Bridge", 10000),
        sl("powered", "Engineering", 8000, 1),
    ],
    "Nebula": [
        sl("base", "Hull", 10000),
        sl("base", "Shield Generator", 10000),
        sl("powered", "Sensor Array", 8000, 100),
        sl("power", "Warp Core", 7000),
        sl("powered", "Impulse Engines", 3000, 100, [
            ch("Port Impulse", 3000), ch("Starboard Impulse", 3000)]),
        sl("powered", "Phasers", 8000, 200, [
            ch("Dorsal Phaser 1"), ch("Dorsal Phaser 2"),
            ch("Dorsal Phaser 3"), ch("Dorsal Phaser 4"),
            ch("Ventral Phaser 1"), ch("Ventral Phaser 2"),
            ch("Ventral Phaser 3"), ch("Ventral Phaser 4")]),
        sl("powered", "Warp Engines", 8000, 0, [
            ch("Port Warp", 4000), ch("Starboard Warp", 4000)]),
        sl("powered", "Torpedoes", 5000, 150, [
            ch("Forward Torp 1"), ch("Forward Torp 2"),
            ch("Forward Torp 3"), ch("Forward Torp 4"),
            ch("Aft Torp 1"), ch("Aft Torp 2")]),
        sl("powered", "Repair", 8000, 1),
        sl("powered", "Tractors", 3000, 400, [
            ch("Aft Tractor"), ch("Forward Tractor")]),
        sl("base", "Bridge", 10000),
    ],
    "Sovereign": [
        sl("base", "Hull", 12000),
        sl("base", "Shield Generator", 10000),
        sl("powered", "Sensor Array", 8000, 150),
        sl("power", "Warp Core", 7000),
        sl("powered", "Impulse Engines", 3000, 200, [
            ch("Port Impulse", 3000), ch("Starboard Impulse", 3000)]),
        sl("powered", "Torpedoes", 6000, 150, [
            ch("Forward Torpedo 1"), ch("Forward Torpedo 2"),
            ch("Forward Torpedo 3"), ch("Forward Torpedo 4"),
            ch("Aft Torpedo 1"), ch("Aft Torpedo 2")]),
        sl("powered", "Repair", 8000, 1),
        sl("base", "Bridge", 10000),
        sl("powered", "Phasers", 8000, 400, [
            ch("Ventral Phaser 3"), ch("Dorsal Phaser 2"),
            ch("Dorsal Phaser 4"), ch("Dorsal Phaser 1"),
            ch("Dorsal Phaser 3"), ch("Ventral Phaser 1"),
            ch("Ventral Phaser 4"), ch("Ventral Phaser 2")]),
        sl("powered", "Tractors", 3000, 700, [
            ch("Aft Tractor 1"), ch("Aft Tractor 2"),
            ch("Forward Tractor 1"), ch("Forward Tractor 2")]),
        sl("powered", "Warp Engines", 8000, 0, [
            ch("Port Warp", 4500), ch("Starboard Warp", 4500)]),
    ],
    "Shuttle": [
        sl("base", "Hull", 3500),
        sl("powered", "Impulse Engines", 1000, 50, [
            ch("Port Impulse", 1000), ch("Starboard Impulse", 1000)]),
        sl("power", "Warp Core", 1500),
        sl("powered", "Sensor Array", 2000, 50),
        sl("base", "Shield Generator", 3500),
        sl("powered", "Phasers", 3000, 50, [
            ch("Phaser")]),
        sl("powered", "Repair", 3000, 1),
        sl("powered", "Warp Engines", 2000, 0, [
            ch("Port Warp", 1000), ch("Starboard Warp", 1000)]),
        sl("powered", "Tractors", 1500, 100, [
            ch("Forward Tractor")]),
    ],
    # === Klingon ===
    "Vorcha": [
        sl("base", "Hull", 12000),
        sl("base", "Shield Generator", 10000),
        sl("power", "Warp Core", 7000),
        sl("powered", "Disruptor Beams", 5000, 50, [
            ch("Disruptor")]),
        sl("powered", "Disruptor Cannons", 5000, 150, [
            ch("Port Cannon"), ch("Star Cannon")]),
        sl("powered", "Torpedoes", 5000, 150, [
            ch("Fwd Torpedo 1"), ch("Fwd Torpedo 2"),
            ch("Aft Torpedo")]),
        sl("powered", "Impulse Engines", 3000, 100, [
            ch("Port Impulse", 3000), ch("Starboard Impulse", 3000)]),
        sl("powered", "Warp Engines", 8000, 0, [
            ch("Port Warp", 4000), ch("Starboard Warp", 4000)]),
        sl("powered", "Cloaking Device", 6000, 700),
        sl("powered", "Sensor Array", 6000, 100),
        sl("powered", "Repair System", 7000, 1),
        sl("powered", "Tractors", 3000, 700, [
            ch("Aft Tractor"), ch("Forward Tractor")]),
    ],
    "BirdOfPrey": [
        sl("base", "Hull", 4000),
        sl("base", "Shield Generator", 4000),
        sl("power", "Warp Core", 3000),
        sl("powered", "Disruptor Cannons", 3000, 80, [
            ch("Port Cannon"), ch("Star Cannon")]),
        sl("powered", "Torpedoes", 2000, 50, [
            ch("Fwd Torpedo")]),
        sl("powered", "Impulse Engines", 2000, 50, [
            ch("Impulse Engine", 2000)]),
        sl("powered", "Warp Engines", 4000, 0, [
            ch("Port Warp", 2000), ch("Starboard Warp", 2000)]),
        sl("powered", "Cloaking Device", 3000, 380),
        sl("powered", "Sensor Array", 3000, 50),
        sl("powered", "Engineering", 4000, 1),
    ],
    # === Romulan ===
    "Warbird": [
        sl("base", "Hull", 14000),
        sl("base", "Shield Generator", 10000),
        sl("power", "Power Plant", 8000),
        sl("powered", "Disruptor Beam", 5000, 100, [
            ch("Disruptor")]),
        sl("powered", "Disruptor Cannons", 5000, 200, [
            ch("Star Cannon 1"), ch("Port Cannon 1"),
            ch("Star Cannon 2"), ch("Port Cannon 2")]),
        sl("powered", "Torpedoes", 5000, 150, [
            ch("Forward Torpedo"), ch("Aft Torpedo")]),
        sl("powered", "Impulse Engines", 3000, 300, [
            ch("Port Impulse", 3000), ch("Starboard Impulse", 3000)]),
        sl("powered", "Warp Engines", 8000, 0, [
            ch("Port Warp", 4000), ch("Starboard Warp", 4000)]),
        sl("powered", "Cloaking Device", 7000, 1000),
        sl("powered", "Sensor Array", 8000, 200),
        sl("powered", "Engineering", 8000, 1),
        sl("base", "Bridge", 10000),
        sl("powered", "Tractors", 3000, 800, [
            ch("Aft Tractor"), ch("Forward Tractor")]),
    ],
    # === Ferengi ===
    "Marauder": [
        sl("base", "Hull", 8000),
        sl("base", "Shield Generator", 8000),
        sl("power", "Warp Core", 5000),
        sl("powered", "Phasers", 5000, 100, [
            ch("Ventral Phaser")]),
        sl("powered", "Impulse Engines", 3000, 50, [
            ch("Port Impulse", 3000), ch("Starboard Impulse", 3000)]),
        sl("powered", "Warp Engines", 6000, 0, [
            ch("Star Warp", 3000), ch("Port Warp", 3000)]),
        sl("powered", "Tractors", 3000, 2000, [
            ch("Forward Tractor"), ch("Aft Tractor")]),
        sl("powered", "Sensor Array", 6000, 100),
        sl("powered", "Repair Subsystem", 6000, 1),
        sl("powered", "Plasma Emitters", 5000, 200, [
            ch("Port Emitter"), ch("Star Emitter")]),
    ],
    # === Cardassian ===
    "Galor": [
        sl("base", "Hull", 8000),
        sl("base", "Shield Generator", 8000),
        sl("power", "Warp Core", 5000),
        sl("powered", "Compressors", 5000, 150, [
            ch("Forward Beam"), ch("Port Beam"),
            ch("Star Beam"), ch("Aft Beam")]),
        sl("powered", "Torpedoes", 3000, 50, [
            ch("Forward Torpedo")]),
        sl("powered", "Impulse Engines", 3000, 50, [
            ch("Port Impulse", 3000), ch("Starboard Impulse", 3000)]),
        sl("powered", "Warp Engine", 4000, 0, [
            ch("Warp Engine 1", 4000)]),
        sl("powered", "Repair Subsystem", 5000, 1),
        sl("powered", "Sensor Array", 5000, 50),
    ],
    "Keldon": [
        sl("base", "Hull", 9000),
        sl("base", "Shield Generator", 9000),
        sl("power", "Warp Core", 6000),
        sl("powered", "Compressors", 5000, 200, [
            ch("Forward Beam"), ch("Port Beam"),
            ch("Star Beam"), ch("Aft Beam")]),
        sl("powered", "Torpedoes", 4000, 70, [
            ch("Forward Torpedo"), ch("Aft Torpedo")]),
        sl("powered", "Impulse Engines", 3000, 70, [
            ch("Impulse 1", 3000), ch("Impulse 2", 3000),
            ch("Impulse 3", 3000), ch("Impulse 4", 3000)]),
        sl("powered", "Warp Engine", 5000, 0, [
            ch("Warp Engine 1", 5000)]),
        sl("powered", "Sensor Array", 6000, 50),
        sl("powered", "Repair Subsystem", 6000, 1),
        sl("powered", "Tractors", 3000, 400, [
            ch("Ventral Tractor"), ch("Dorsal Tractor")]),
    ],
    "CardHybrid": [
        sl("base", "Hull", 12000),
        sl("power", "Warp Core", 7000),
        sl("powered", "Torpedoes", 5000, 100, [
            ch("Torpedo 1"), ch("Torpedo 2"), ch("Aft Torpedo")]),
        sl("powered", "Repair System", 7000, 1),
        sl("base", "Shield Generator", 10000),
        sl("powered", "Sensor Array", 7000, 100),
        sl("powered", "Impulse Engines", 3000, 100, [
            ch("Port Impulse", 3000), ch("Starboard Impulse", 3000)]),
        sl("powered", "Warp Engines", 8000, 0, [
            ch("Port Warp", 4000), ch("Starboard Warp", 4000),
            ch("Center Warp", 4000)]),
        sl("powered", "Beams", 7000, 300, [
            ch("Forward Compressor"),
            ch("Forward Beam 1"), ch("Forward Beam 2"),
            ch("Ventral Beam 1"), ch("Ventral Beam 2"),
            ch("Dorsal Beam 1"), ch("Dorsal Beam 2")]),
        sl("powered", "Disruptor Cannons", 5000, 100, [
            ch("Cannon")]),
        sl("powered", "Tractors", 3000, 400, [
            ch("Forward Tractor"), ch("Aft Tractor")]),
    ],
    # === Kessok ===
    "KessokHeavy": [
        sl("base", "Hull", 16000),
        sl("power", "Warp Core", 8000),
        sl("powered", "Impulse Engines", 3000, 200, [
            ch("Port Impulse", 3000), ch("Starboard Impulse", 3000)]),
        sl("powered", "Warp Engines", 8000, 0, [
            ch("Port Warp", 4000), ch("Starboard Warp", 4000)]),
        sl("powered", "Positron Beams", 8000, 200, [
            ch("Forward Beam 1"), ch("Forward Beam 2"),
            ch("Ventral Beam 1"), ch("Ventral Beam 2"),
            ch("Dorsal Beam 1"), ch("Dorsal Beam 2"),
            ch("Forward Beam 3"), ch("Forward Beam 4")]),
        sl("powered", "Torpedoes", 5000, 200, [
            ch("Torpedo Tube 1"), ch("Torpedo Tube 2")]),
        sl("powered", "Repair System", 8000, 50),
        sl("base", "Shield Generator", 12000),
        sl("powered", "Sensor Array", 8000, 200),
        sl("powered", "Cloaking Device", 7000, 1300),
    ],
    "KessokLight": [
        sl("base", "Hull", 10000),
        sl("power", "Warp Core", 6000),
        sl("powered", "Torpedoes", 4000, 100, [
            ch("Torpedo")]),
        sl("powered", "Repair System", 6000, 1),
        sl("base", "Shield Generator", 8000),
        sl("powered", "Sensor Array", 6000, 100),
        sl("powered", "Impulse Engines", 3000, 100, [
            ch("Port Impulse", 3000), ch("Starboard Impulse", 3000)]),
        sl("powered", "Warp Engines", 6000, 0, [
            ch("Port Warp", 3000), ch("Starboard Warp", 3000)]),
        sl("powered", "Beams", 7000, 300, [
            ch("Forward Beam 1"), ch("Forward Beam 2"),
            ch("Port Beam 1"), ch("Port Beam 2"),
            ch("Star Beam 1"), ch("Star Beam 2"),
            ch("Aft Beam 1"), ch("Aft Beam 2")]),
        sl("powered", "Cloaking Device", 5000, 700),
    ],
    # === Civilian ===
    "CardFreighter": [
        sl("base", "Hull", 5000),
        sl("powered", "Impulse Engines", 2000, 50, [
            ch("Port Impulse", 2000), ch("Starboard Impulse", 2000)]),
        sl("powered", "Warp Engines", 3000, 0, [
            ch("Warp Drive", 3000)]),
        sl("powered", "Engineering", 3000, 1),
        sl("powered", "Tractors", 1500, 200, [
            ch("Tractor Beam")]),
        sl("powered", "Sensor Array", 3000, 50),
        sl("base", "Shield Generator", 4000),
        sl("power", "Warp Core", 3000),
    ],
}

def validate(data):
    """Verify all child names that should match flat subsystem names do match."""
    issues = []
    for ship in data["ships"]:
        name = ship["name"]
        if name not in SER_LISTS:
            continue
        flat_names = {s["name"] for s in ship["subsystems"]}
        for entry in SER_LISTS[name]:
            for child in entry.get("children", []):
                cn = child["name"]
                if cn not in flat_names and "max_condition" not in child:
                    issues.append(f"  {name}: child '{cn}' not in flat array and no max_condition")
    return issues

def main():
    if len(sys.argv) < 2:
        print("Usage: add_ser_lists.py <monolith-registry.json>", file=sys.stderr)
        sys.exit(2)
    path = sys.argv[1]
    with open(path, "r") as f:
        data = json.load(f)

    issues = validate(data)
    if issues:
        print("VALIDATION ERRORS:", file=sys.stderr)
        for i in issues:
            print(i, file=sys.stderr)
        sys.exit(1)

    for ship in data["ships"]:
        name = ship["name"]
        if name not in POWER_PARAMS:
            print(f"WARNING: No power params for {name}", file=sys.stderr)
            continue
        if name not in SER_LISTS:
            print(f"WARNING: No serialization list for {name}", file=sys.stderr)
            continue

        po, mbl, bbl, mcc, bcc = POWER_PARAMS[name]
        ship["power_output"] = float(po)
        ship["main_battery_limit"] = float(mbl)
        ship["backup_battery_limit"] = float(bbl)
        ship["main_conduit_capacity"] = float(mcc)
        ship["backup_conduit_capacity"] = float(bcc)
        ship["serialization_list"] = SER_LISTS[name]

    with open(path, "w") as f:
        json.dump(data, f, indent=2)
        f.write("\n")

    print(f"Updated {len(SER_LISTS)} ships with serialization_list + power params")

if __name__ == "__main__":
    main()
