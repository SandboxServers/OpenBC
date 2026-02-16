#!/usr/bin/env python3
"""
BC Data Scraper -- Extract ship stats and projectile data from Bridge Commander
reference scripts and output as JSON for the OpenBC game registry.

Parses auto-generated .py hardpoint files using regex (not import) since
they depend on App/GlobalPropertyTemplates modules we don't have.

Usage:
    python3 tools/scrape_bc.py <scripts_dir> [-o output.json]
    python3 tools/scrape_bc.py ../STBC-Dedicated-Server/reference/scripts/ -o data/vanilla-1.1.json
"""

import argparse
import json
import os
import re
import sys

# The 16 flyable ships (indices 1-16 from SpeciesToShip.py)
FLYABLE_SHIPS = [
    (1,  "Akira",      "akira",      "Federation"),
    (2,  "Ambassador", "ambassador", "Federation"),
    (3,  "Galaxy",     "galaxy",     "Federation"),
    (4,  "Nebula",     "nebula",     "Federation"),
    (5,  "Sovereign",  "sovereign",  "Federation"),
    (6,  "BirdOfPrey", "birdofprey", "Klingon"),
    (7,  "Vorcha",     "vorcha",     "Klingon"),
    (8,  "Warbird",    "warbird",    "Romulan"),
    (9,  "Marauder",   "marauder",   "Ferengi"),
    (10, "Galor",      "galor",      "Cardassian"),
    (11, "Keldon",     "keldon",     "Cardassian"),
    (12, "CardHybrid", "cardhybrid", "Cardassian"),
    (13, "KessokHeavy","kessokheavy","Kessok"),
    (14, "KessokLight","kessoklight","Kessok"),
    (15, "Shuttle",    "shuttle",    "Federation"),
    (16, "CardFreighter","cardfreighter","Cardassian"),
]

# Property type -> subsystem category
PROP_TYPES = {
    "HullProperty":              "hull",
    "ShieldProperty":            "shield",
    "ImpulseEngineProperty":     "impulse_engine",
    "WarpEngineProperty":        "warp_engine",
    "RepairSubsystemProperty":   "repair",
    "SensorProperty":            "sensor",
    "PowerProperty":             "power",
    "ViewScreenProperty":        "viewscreen",
    "PhaserProperty":            "phaser",
    "PulseWeaponProperty":       "pulse_weapon",
    "TorpedoTubeProperty":       "torpedo_tube",
    "TractorBeamProperty":       "tractor_beam",
    "CloakingSubsystemProperty": "cloak",
    "PoweredSubsystemProperty":  "powered",
    "CrewProperty":              "crew",
    "BridgeProperty":            "bridge",
    "LifeSupportSubsystemProperty": "life_support",
}

# Shield facing names
SHIELD_FACINGS = ["FRONT_SHIELDS", "REAR_SHIELDS", "TOP_SHIELDS",
                  "BOTTOM_SHIELDS", "LEFT_SHIELDS", "RIGHT_SHIELDS"]
SHIELD_IDX = {name: i for i, name in enumerate(SHIELD_FACINGS)}


def parse_float(s):
    """Parse a float, handling various formats."""
    try:
        return float(s)
    except (ValueError, TypeError):
        return 0.0


def parse_int(s):
    """Parse an int."""
    try:
        return int(float(s))
    except (ValueError, TypeError):
        return 0


def scrape_hardpoint(filepath):
    """Parse a single hardpoint .py file and extract all subsystems."""
    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    subsystems = []
    hull_hp = 0.0
    mass = 0.0
    rotational_inertia = 0.0
    max_speed = 0.0
    max_accel = 0.0
    max_angular_accel = 0.0
    max_angular_velocity = 0.0
    shield_hp = [0.0] * 6
    shield_recharge = [0.0] * 6
    can_cloak = False
    has_tractor = False
    max_repair_points = 0.0
    num_repair_teams = 0

    # Find all property creations: VarName = App.TypeProperty_Create("Name")
    create_re = re.compile(
        r'^(\w+)\s*=\s*App\.(\w+)_Create\s*\(\s*"([^"]+)"\s*\)',
        re.MULTILINE
    )

    for match in create_re.finditer(text):
        var_name = match.group(1)
        prop_type = match.group(2)
        prop_name = match.group(3)

        if prop_type not in PROP_TYPES:
            continue

        category = PROP_TYPES[prop_type]

        # Extract all Set* calls for this variable
        subsys = {
            "name": prop_name,
            "type": category,
            "position": [0.0, 0.0, 0.0],
            "radius": 0.0,
            "max_condition": 0.0,
            "disabled_pct": 0.0,
            "is_critical": False,
            "is_targetable": False,
            "repair_complexity": 0.0,
        }

        # Common Set* patterns
        def get_set(method, default=None):
            pat = re.compile(
                rf'^{re.escape(var_name)}\.{method}\s*\(\s*([^)]+)\)',
                re.MULTILINE
            )
            m = pat.search(text)
            if m:
                return m.group(1).strip()
            return default

        subsys["max_condition"] = parse_float(get_set("SetMaxCondition", "0"))
        subsys["is_critical"] = parse_int(get_set("SetCritical", "0")) != 0
        subsys["is_targetable"] = parse_int(get_set("SetTargetable", "0")) != 0
        subsys["repair_complexity"] = parse_float(get_set("SetRepairComplexity", "0"))
        subsys["disabled_pct"] = parse_float(get_set("SetDisabledPercentage", "0"))
        subsys["radius"] = parse_float(get_set("SetRadius", "0"))

        # Position: SetPosition(x, y, z)
        pos_m = re.search(
            rf'^{re.escape(var_name)}\.SetPosition\s*\(\s*([^,]+),\s*([^,]+),\s*([^)]+)\)',
            text, re.MULTILINE
        )
        if pos_m:
            subsys["position"] = [
                parse_float(pos_m.group(1)),
                parse_float(pos_m.group(2)),
                parse_float(pos_m.group(3)),
            ]

        # Hull-specific: only the subsystem named "Hull" sets top-level hull_hp
        # (other HullProperty subsystems like "Bridge" are separate)
        if category == "hull" and prop_name == "Hull":
            hull_hp = subsys["max_condition"]

        # Shield-specific
        if category == "shield":
            for facing in SHIELD_FACINGS:
                hp_m = re.search(
                    rf'{re.escape(var_name)}\.SetMaxShields\s*\(\s*{re.escape(var_name)}\.{facing}\s*,\s*([^)]+)\)',
                    text
                )
                if hp_m:
                    shield_hp[SHIELD_IDX[facing]] = parse_float(hp_m.group(1))

                rc_m = re.search(
                    rf'{re.escape(var_name)}\.SetShieldChargePerSecond\s*\(\s*{re.escape(var_name)}\.{facing}\s*,\s*([^)]+)\)',
                    text
                )
                if rc_m:
                    shield_recharge[SHIELD_IDX[facing]] = parse_float(rc_m.group(1))

        # Engine-specific
        if category == "impulse_engine":
            max_speed = parse_float(get_set("SetMaxSpeed", "0"))
            max_accel = parse_float(get_set("SetMaxAccel", "0"))
            max_angular_accel = parse_float(get_set("SetMaxAngularAccel", "0"))
            max_angular_velocity = parse_float(get_set("SetMaxAngularVelocity", "0"))

        # Weapon-specific (phaser, pulse, torpedo, tractor)
        if category in ("phaser", "pulse_weapon", "tractor_beam"):
            subsys["max_damage"] = parse_float(get_set("SetMaxDamage", "0"))
            subsys["max_charge"] = parse_float(get_set("SetMaxCharge", "0"))
            subsys["min_firing_charge"] = parse_float(get_set("SetMinFiringCharge", "0"))
            subsys["recharge_rate"] = parse_float(get_set("SetRechargeRate", "0"))
            subsys["discharge_rate"] = parse_float(get_set("SetNormalDischargeRate", "0"))
            subsys["max_damage_distance"] = parse_float(get_set("SetMaxDamageDistance", "0"))
            subsys["weapon_id"] = parse_int(get_set("SetWeaponID", "0"))

            # Orientation
            fwd_m = re.search(
                rf'^{re.escape(var_name)}Forward\s*=\s*App\.TGPoint3\(\).*?'
                rf'{re.escape(var_name)}Forward\.SetXYZ\s*\(\s*([^,]+),\s*([^,]+),\s*([^)]+)\)',
                text, re.MULTILINE | re.DOTALL
            )
            if fwd_m:
                subsys["forward"] = [
                    parse_float(fwd_m.group(1)),
                    parse_float(fwd_m.group(2)),
                    parse_float(fwd_m.group(3)),
                ]

            up_m = re.search(
                rf'^{re.escape(var_name)}Up\s*=\s*App\.TGPoint3\(\).*?'
                rf'{re.escape(var_name)}Up\.SetXYZ\s*\(\s*([^,]+),\s*([^,]+),\s*([^)]+)\)',
                text, re.MULTILINE | re.DOTALL
            )
            if up_m:
                subsys["up"] = [
                    parse_float(up_m.group(1)),
                    parse_float(up_m.group(2)),
                    parse_float(up_m.group(3)),
                ]

            # Arc angles
            aw_m = re.search(
                rf'{re.escape(var_name)}\.SetArcWidthAngles\s*\(\s*([^,]+),\s*([^)]+)\)',
                text
            )
            if aw_m:
                subsys["arc_width"] = [
                    parse_float(aw_m.group(1)),
                    parse_float(aw_m.group(2)),
                ]

            ah_m = re.search(
                rf'{re.escape(var_name)}\.SetArcHeightAngles\s*\(\s*([^,]+),\s*([^)]+)\)',
                text
            )
            if ah_m:
                subsys["arc_height"] = [
                    parse_float(ah_m.group(1)),
                    parse_float(ah_m.group(2)),
                ]

        if category == "tractor_beam":
            has_tractor = True
            subsys["normal_power"] = parse_float(get_set("SetNormalPowerPerSecond", "0"))

        if category == "torpedo_tube":
            subsys["reload_delay"] = parse_float(get_set("SetReloadDelay", "0"))
            subsys["max_ready"] = parse_int(get_set("SetMaxReady", "0"))
            subsys["immediate_delay"] = parse_float(get_set("SetImmediateDelay", "0"))
            subsys["weapon_id"] = parse_int(get_set("SetWeaponID", "0"))
            subsys["dumbfire"] = parse_int(get_set("SetDumbfire", "0")) != 0

            # Direction
            dir_m = re.search(
                rf'^{re.escape(var_name)}Direction\s*=\s*App\.TGPoint3\(\).*?'
                rf'{re.escape(var_name)}Direction\.SetXYZ\s*\(\s*([^,]+),\s*([^,]+),\s*([^)]+)\)',
                text, re.MULTILINE | re.DOTALL
            )
            if dir_m:
                subsys["direction"] = [
                    parse_float(dir_m.group(1)),
                    parse_float(dir_m.group(2)),
                    parse_float(dir_m.group(3)),
                ]

        if category == "cloak":
            can_cloak = True
            subsys["cloak_strength"] = parse_float(get_set("SetCloakStrength", "0"))
            subsys["normal_power"] = parse_float(get_set("SetNormalPowerPerSecond", "0"))

        if category == "repair":
            max_repair_points = parse_float(get_set("SetMaxRepairPoints", "0"))
            num_repair_teams = parse_int(get_set("SetNumRepairTeams", "0"))
            subsys["max_repair_points"] = max_repair_points
            subsys["num_repair_teams"] = num_repair_teams

        subsystems.append(subsys)

    # Count weapon types
    torpedo_tubes = sum(1 for s in subsystems if s["type"] == "torpedo_tube")
    phaser_banks = sum(1 for s in subsystems if s["type"] == "phaser")
    pulse_weapons = sum(1 for s in subsystems if s["type"] == "pulse_weapon")
    tractor_beams = sum(1 for s in subsystems if s["type"] == "tractor_beam")

    # Extract mass and rotational inertia (called on ship object in hardpoint)
    m = re.search(r"\.SetMass\s*\(\s*([^)]+)\)", text)
    if m:
        mass = parse_float(m.group(1))
    m = re.search(r"\.SetRotationalInertia\s*\(\s*([^)]+)\)", text)
    if m:
        rotational_inertia = parse_float(m.group(1))

    return {
        "hull_hp": hull_hp,
        "mass": mass,
        "rotational_inertia": rotational_inertia,
        "shield_hp": shield_hp,
        "shield_recharge": shield_recharge,
        "max_speed": max_speed,
        "max_accel": max_accel,
        "max_angular_accel": max_angular_accel,
        "max_angular_velocity": max_angular_velocity,
        "can_cloak": can_cloak,
        "has_tractor": has_tractor,
        "torpedo_tubes": torpedo_tubes,
        "phaser_banks": phaser_banks,
        "pulse_weapons": pulse_weapons,
        "tractor_beams": tractor_beams,
        "max_repair_points": max_repair_points,
        "num_repair_teams": num_repair_teams,
        "subsystems": subsystems,
    }


def scrape_ships(scripts_dir):
    """Scrape all 16 flyable ships."""
    ships = []
    hardpoints_dir = os.path.join(scripts_dir, "ships", "Hardpoints")

    for species_id, name, filename, faction in FLYABLE_SHIPS:
        hp_path = os.path.join(hardpoints_dir, f"{filename}.py")
        if not os.path.exists(hp_path):
            print(f"  WARNING: {hp_path} not found, skipping {name}", file=sys.stderr)
            continue

        data = scrape_hardpoint(hp_path)

        ship = {
            "name": name,
            "species_id": species_id,
            "faction": faction,
            "hull_hp": data["hull_hp"],
            "mass": data["mass"],
            "rotational_inertia": data["rotational_inertia"],
            "max_speed": data["max_speed"],
            "max_accel": data["max_accel"],
            "max_angular_accel": data["max_angular_accel"],
            "max_angular_velocity": data["max_angular_velocity"],
            "shield_hp": data["shield_hp"],
            "shield_recharge": data["shield_recharge"],
            "can_cloak": data["can_cloak"],
            "has_tractor": data["has_tractor"],
            "torpedo_tubes": data["torpedo_tubes"],
            "phaser_banks": data["phaser_banks"],
            "pulse_weapons": data["pulse_weapons"],
            "tractor_beams": data["tractor_beams"],
            "max_repair_points": data["max_repair_points"],
            "num_repair_teams": data["num_repair_teams"],
            "subsystem_count": len(data["subsystems"]),
            "subsystems": data["subsystems"],
        }
        ships.append(ship)
        print(f"  {name}: hull={data['hull_hp']:.0f}, "
              f"shields={sum(data['shield_hp']):.0f}, "
              f"{len(data['subsystems'])} subsystems, "
              f"cloak={data['can_cloak']}, tractor={data['has_tractor']}",
              file=sys.stderr)

    return ships


def main():
    parser = argparse.ArgumentParser(description="Scrape BC reference scripts to JSON")
    parser.add_argument("scripts_dir", help="Path to BC scripts/ directory")
    parser.add_argument("-o", "--output", default=None,
                        help="Output JSON file (default: stdout)")
    args = parser.parse_args()

    if not os.path.isdir(args.scripts_dir):
        print(f"Error: {args.scripts_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    print("Scraping ships...", file=sys.stderr)
    ships = scrape_ships(args.scripts_dir)
    print(f"Found {len(ships)} ships", file=sys.stderr)

    registry = {
        "version": "vanilla-1.1",
        "ships": ships,
    }

    output = json.dumps(registry, indent=2)

    if args.output:
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        with open(args.output, "w") as f:
            f.write(output)
            f.write("\n")
        print(f"Written to {args.output}", file=sys.stderr)
    else:
        print(output)


if __name__ == "__main__":
    main()
