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
import struct
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


# Torpedo types from SpeciesToTorp.py (ID -> script name -> actual filename)
TORP_TYPES = [
    (1,  "DISRUPTOR",           "Disruptor"),
    (2,  "PHOTON",              "PhotonTorpedo"),
    (3,  "QUANTUM",             "QuantumTorpedo"),
    (4,  "ANTIMATTER",          "AntimatterTorpedo"),
    (5,  "CARDTORP",            "CardassianTorpedo"),
    (6,  "KLINGONTORP",         "KlingonTorpedo"),
    (7,  "POSITRON",            "PositronTorpedo"),
    (8,  "PULSEDISRUPT",        "PulseDisruptor"),
    (9,  "FUSIONBOLT",          "FusionBolt"),
    (10, "CARDASSIANDISRUPTOR", "CardassianDisruptor"),
    (11, "KESSOKDISRUPTOR",     "KessokDisruptor"),
    (12, "PHASEDPLASMA",        "PhasedPlasma"),
    (13, "POSITRON2",           "PositronTorpedo2"),
    (14, "PHOTON2",             "PhotonTorpedo2"),
    (15, "ROMULANCANNON",       "RomulanCannon"),
]


def scrape_projectile(filepath):
    """Parse a single projectile .py file and extract stats."""
    with open(filepath, "r", encoding="utf-8", errors="replace") as f:
        text = f.read()

    def get_return(func_name):
        m = re.search(
            rf'^def {func_name}\s*\(\s*\):\s*\n\s*return\s*\(?\s*([^)\n]+)',
            text, re.MULTILINE
        )
        return m.group(1).strip() if m else None

    damage = parse_float(get_return("GetDamage"))
    launch_speed = parse_float(get_return("GetLaunchSpeed"))
    power_cost = parse_float(get_return("GetPowerCost"))
    guidance_lifetime = parse_float(get_return("GetGuidanceLifetime"))
    max_angular_accel = parse_float(get_return("GetMaxAngularAccel"))
    lifetime = parse_float(get_return("GetLifetime"))

    # DamageRadiusFactor is set in Create(), not a getter
    drf_m = re.search(r"\.SetDamageRadiusFactor\s*\(\s*([^)]+)\)", text)
    damage_radius_factor = parse_float(drf_m.group(1)) if drf_m else 0.0

    name_val = get_return("GetName")
    if name_val:
        name_val = name_val.strip('"').strip("'")
    else:
        name_val = os.path.splitext(os.path.basename(filepath))[0]

    return {
        "name": name_val,
        "damage": damage,
        "launch_speed": launch_speed,
        "power_cost": power_cost,
        "guidance_lifetime": guidance_lifetime,
        "max_angular_accel": max_angular_accel,
        "lifetime": lifetime,
        "damage_radius_factor": damage_radius_factor,
    }


def scrape_projectiles(scripts_dir):
    """Scrape all 15 projectile types."""
    proj_dir = os.path.join(scripts_dir, "Tactical", "Projectiles")
    projectiles = []

    for net_type_id, const_name, script_name in TORP_TYPES:
        path = os.path.join(proj_dir, f"{script_name}.py")
        if not os.path.exists(path):
            print(f"  WARNING: {path} not found, skipping {const_name}",
                  file=sys.stderr)
            continue

        data = scrape_projectile(path)
        data["net_type_id"] = net_type_id
        data["script"] = script_name
        guided = data["guidance_lifetime"] > 0
        projectiles.append(data)
        print(f"  {script_name}: dmg={data['damage']:.0f}, "
              f"speed={data['launch_speed']:.1f}, "
              f"guided={'%.1fs' % data['guidance_lifetime'] if guided else 'no'}",
              file=sys.stderr)

    return projectiles


# --- Hash functions (Python reimplementation matching C code) ---

HASH_TABLE_0 = bytes([
    0x1D,0x50,0x7E,0x03,0x94,0xCE,0x97,0x7F,0x2A,0x6B,0xF1,0x01,0x27,0x6D,0x8B,0x13,
    0x9A,0xFE,0x90,0x33,0x81,0xD1,0x1E,0x48,0xDD,0xC3,0x9B,0x85,0x8E,0x9D,0x37,0x18,
    0x36,0x42,0x07,0x78,0xB5,0xD6,0x82,0x74,0xD9,0xAB,0x10,0x8A,0xE2,0x21,0x61,0xC0,
    0x58,0x72,0xF7,0x88,0x57,0xED,0x32,0x16,0x5A,0xF9,0x79,0xA1,0xBA,0xFC,0xAC,0x30,
    0xBC,0xB4,0x76,0x26,0xBF,0xCA,0x6A,0xA4,0x2C,0xF3,0x38,0x25,0xB8,0xB0,0x66,0x5D,
    0x1B,0x40,0x96,0xC1,0xBE,0x15,0x9C,0x39,0xEC,0xB6,0x1A,0xA5,0xA0,0x4A,0x54,0xCC,
    0x5C,0xDC,0x5F,0xB7,0xA7,0x22,0xD5,0x4C,0xDF,0x6E,0x9E,0xDA,0x0B,0x06,0x4B,0x44,
    0xAF,0x2B,0x75,0x86,0x64,0xA8,0x53,0x65,0x83,0xD7,0xAA,0x92,0x95,0x47,0xFF,0x08,
    0xCD,0xB1,0xD4,0x35,0x8F,0x3D,0x0F,0x00,0xC5,0x19,0x43,0xC6,0xC2,0x84,0x04,0x24,
    0xAD,0x71,0xEB,0x69,0xE8,0x7A,0xE1,0x0D,0x60,0x4D,0x4F,0x31,0xCB,0xC9,0xC8,0x8C,
    0x5E,0xFD,0x17,0xCF,0x09,0x52,0xE3,0x3B,0x29,0x0A,0x73,0x56,0x68,0xF2,0xD0,0x3E,
    0x0C,0x91,0xE4,0x49,0x14,0x9F,0x67,0x63,0xA6,0xE6,0xBB,0x3C,0xD2,0x89,0xFA,0xEA,
    0xAE,0x77,0x20,0x6F,0x8D,0x05,0x12,0x2D,0x5B,0x93,0x99,0xA3,0xE5,0xEE,0xBD,0xC4,
    0xF6,0x59,0x23,0x7D,0xD8,0x4E,0xE0,0xD3,0xA9,0x6C,0xB2,0x1C,0x55,0x2E,0x11,0x46,
    0x3F,0x70,0xF5,0x98,0xEF,0xE9,0x0E,0x62,0xF4,0xF0,0x80,0x1F,0x02,0x34,0x7B,0xE7,
    0xDE,0xDB,0x45,0x51,0xB9,0x2F,0xC7,0xF8,0x7C,0x41,0x28,0x3A,0xA2,0x87,0xB3,0xFB,
])

HASH_TABLE_1 = bytes([
    0x9F,0x8F,0xDF,0xB9,0xD9,0x1D,0x45,0x44,0xE1,0x76,0xB2,0xDC,0x77,0xE7,0x0F,0xC9,
    0x72,0xC3,0x2A,0xB4,0x06,0x13,0x3F,0x7E,0x8A,0x7B,0xE0,0xC8,0x31,0xDB,0x64,0x0D,
    0x3C,0x80,0x2E,0x5C,0xF5,0xCA,0x97,0x5F,0x79,0x49,0xA6,0xF0,0xBD,0x61,0x08,0x75,
    0x4E,0x99,0x0E,0x2C,0xA5,0x5D,0x40,0x55,0xF4,0xAE,0x90,0x05,0x0A,0x33,0xA7,0x5E,
    0x57,0xE6,0x9B,0xB5,0x6C,0x6A,0x54,0x8B,0xAF,0x69,0xCE,0x7A,0xCF,0xE3,0x9C,0x7F,
    0x59,0x9A,0x88,0xC4,0x98,0xA8,0x21,0x37,0xFA,0x03,0x5A,0x82,0x26,0xAB,0x93,0xB7,
    0x1F,0x41,0xFF,0x78,0xEB,0xD0,0xA3,0xBC,0xDD,0x73,0xF8,0xD3,0xA0,0xAD,0xEE,0x94,
    0x0C,0x20,0xC2,0x6D,0x0B,0xB3,0x02,0xC5,0x34,0x56,0xF3,0x6F,0x81,0xE4,0xBA,0x3B,
    0xF2,0x12,0xCB,0x58,0xD6,0x1C,0xED,0x7D,0xBB,0x67,0x8D,0x2B,0x1A,0x04,0xAC,0xFE,
    0x25,0x51,0x23,0x46,0x60,0x27,0xA9,0x36,0xA2,0x63,0x18,0x39,0xD2,0xB1,0x7C,0x00,
    0xC1,0x71,0x8E,0x62,0x5B,0xDE,0xB6,0xB8,0xF1,0x9E,0x10,0x84,0x11,0xF9,0x17,0xB0,
    0xAA,0xC7,0x15,0xD7,0x8C,0x3E,0x38,0x29,0x24,0x09,0x70,0x52,0x65,0x42,0xD4,0x48,
    0x92,0x1E,0xD1,0xE5,0x4D,0x4C,0x4B,0xD5,0x1B,0x74,0x3A,0xBE,0x89,0x95,0x16,0x01,
    0x96,0x9D,0xEF,0x28,0x32,0x50,0x30,0xF6,0x47,0x2F,0xE9,0x4F,0x35,0xC0,0x22,0xEC,
    0x43,0x53,0x14,0x4A,0xA4,0xE2,0xEA,0x83,0x6E,0x07,0x2D,0x87,0x68,0xDA,0x86,0xF7,
    0xFC,0xE8,0x66,0xCC,0x91,0x85,0xCD,0x19,0x6B,0xFB,0xC6,0xA1,0xBF,0xD8,0x3D,0xFD,
])

HASH_TABLE_2 = bytes([
    0x35,0x85,0x49,0xE2,0xA7,0x42,0xDF,0x0B,0x2D,0x23,0xDD,0xDE,0x1F,0x17,0xBB,0xCF,
    0x4E,0xA3,0x19,0x04,0x71,0x12,0xB5,0x50,0x43,0x64,0xA0,0x15,0xDB,0x22,0xB0,0x83,
    0x39,0xEA,0xAF,0xC3,0xD0,0xCE,0x77,0x14,0xAD,0x56,0x80,0x5F,0x6E,0xD2,0xD9,0xC0,
    0xE6,0xF6,0x70,0xF9,0x05,0x5A,0x33,0xC5,0x8C,0x73,0xCB,0xFA,0x81,0x3E,0xD8,0x9E,
    0x26,0xD6,0x0C,0xBA,0xAA,0xCD,0x7E,0x9D,0xFF,0x1D,0x06,0xC4,0xED,0xF2,0xF4,0x5B,
    0x94,0x9B,0xA1,0x5E,0xB8,0x37,0xC1,0xF1,0x57,0x7B,0xD7,0xFB,0x25,0xCC,0x91,0xF0,
    0x62,0x7F,0xFC,0x1A,0x96,0x72,0x2F,0xDA,0x38,0xA2,0x3A,0xBF,0xB4,0xB1,0xE8,0xBD,
    0x0F,0xF7,0xAE,0xA6,0x88,0x74,0x2C,0x7D,0x01,0xEC,0x07,0x24,0x40,0x34,0x5D,0x59,
    0x9C,0x7A,0x9A,0xEE,0xE7,0x46,0x9F,0x61,0x63,0x30,0xB2,0x97,0xEF,0xAC,0x76,0x8E,
    0x75,0xE4,0xD3,0xA9,0x2A,0x41,0x00,0xA5,0xBC,0x66,0x51,0xCA,0x1B,0xB7,0x7C,0x0E,
    0x18,0x6B,0xC7,0x78,0x84,0x6A,0x6C,0x82,0x60,0xD5,0x1C,0x13,0x55,0x52,0xB9,0x53,
    0x32,0x1E,0xB6,0x28,0x4B,0x8F,0x11,0x8D,0x8B,0xFD,0x10,0x67,0x3F,0xD1,0x36,0x45,
    0x86,0xC9,0x4A,0x54,0x4F,0xF8,0x79,0x29,0x69,0x08,0xE9,0x89,0x20,0xAB,0x6D,0xE3,
    0xC6,0x98,0x99,0xE5,0x93,0x48,0x09,0xE1,0xF3,0x47,0x4C,0xFE,0x8A,0x95,0x3C,0xEB,
    0x2B,0x03,0xF5,0xA8,0x58,0x3D,0xC2,0x31,0x65,0xDC,0x27,0xBE,0x21,0x68,0xE0,0xB3,
    0xC8,0xA4,0x02,0x2E,0xD4,0x3B,0x6F,0x5C,0x87,0x0A,0x92,0x0D,0x4D,0x16,0x44,0x90,
])

HASH_TABLE_3 = bytes([
    0x5F,0xD5,0xB8,0xF3,0x68,0x63,0xB3,0xE6,0xCE,0x33,0x02,0x6A,0x99,0xD0,0x12,0xA7,
    0x2F,0x0E,0xAD,0xF7,0xA2,0x0C,0x60,0xAB,0x4E,0x75,0xD7,0x6F,0x26,0xD8,0x1A,0xB5,
    0x30,0xA6,0xEB,0x11,0x2E,0x61,0x9F,0x7A,0xA3,0x8E,0xA0,0xDF,0x43,0xB7,0x8D,0xCA,
    0x4A,0x5A,0x98,0xF1,0x66,0x38,0x1C,0xD1,0x5D,0xF0,0xCF,0xB1,0x74,0xCD,0x9D,0xB9,
    0x3E,0x8C,0xE7,0x31,0xEC,0x7F,0x0F,0x2C,0x7C,0x71,0x6D,0x8B,0xFE,0xC3,0x23,0xC0,
    0xB4,0xC6,0x7D,0xC9,0xBD,0x3D,0x04,0xF2,0x5E,0x03,0xE8,0xD2,0xDD,0x53,0xFB,0x64,
    0xDC,0xD4,0x90,0xDA,0x08,0x51,0x78,0xBC,0xE4,0xC1,0xA1,0x29,0x36,0xCC,0x1B,0x87,
    0x95,0x8F,0xE9,0x97,0xD9,0x80,0x6E,0xF4,0x94,0xDB,0x10,0xE2,0xAE,0xBB,0x93,0xD3,
    0x57,0x5B,0x2D,0x0B,0x01,0x7B,0xF6,0x40,0x1E,0x09,0x9B,0x67,0x83,0x96,0xB2,0xFC,
    0x46,0x89,0xB6,0x9C,0xC4,0x69,0x70,0x54,0x24,0x05,0x28,0xDE,0x17,0xEE,0xA8,0xE5,
    0x77,0x48,0x16,0x0D,0x06,0x18,0x42,0x5C,0x1D,0xFD,0xC5,0x50,0xAA,0x4D,0xEA,0x21,
    0x59,0x9E,0xF8,0x73,0x14,0x82,0x32,0x3C,0xEF,0xE3,0x15,0x8A,0x07,0x41,0x56,0xC8,
    0x79,0x22,0x45,0x92,0xA5,0x1F,0xC7,0x2A,0x85,0x19,0xBE,0xE0,0x7E,0x25,0xBF,0x9A,
    0x0A,0x47,0x84,0xED,0xB0,0x81,0x72,0x6B,0x52,0xD6,0xFF,0x44,0x3F,0xA9,0xF9,0xC2,
    0x37,0x20,0xAF,0x3A,0xCB,0x62,0x88,0x86,0x4B,0x76,0x2B,0x91,0x58,0xA4,0xFA,0xF5,
    0x55,0x49,0x4F,0xBA,0xE1,0x39,0x4C,0x13,0x65,0x3B,0x34,0x6C,0x00,0x35,0xAC,0x27,
])


def string_hash(s):
    """4-lane Pearson hash matching C string_hash()."""
    h0 = h1 = h2 = h3 = 0
    for c in s.encode("ascii"):
        h0 = HASH_TABLE_0[c ^ h0]
        h1 = HASH_TABLE_1[c ^ h1]
        h2 = HASH_TABLE_2[c ^ h2]
        h3 = HASH_TABLE_3[c ^ h3]
    return (h0 << 24) | (h1 << 16) | (h2 << 8) | h3


def file_hash(data):
    """Rotate-XOR hash matching C file_hash(). Skips DWORD index 1 (.pyc timestamp)."""
    h = 0
    dword_count = len(data) // 4
    for i in range(dword_count):
        if i == 1:
            continue
        dword = struct.unpack_from("<I", data, i * 4)[0]
        h ^= dword
        h = ((h << 1) | (h >> 31)) & 0xFFFFFFFF
    remainder = len(data) % 4
    if remainder > 0:
        tail = data[dword_count * 4:]
        for b in tail:
            extended = b if b < 128 else b - 256  # sign-extend
            h ^= extended & 0xFFFFFFFF
            h = ((h << 1) | (h >> 31)) & 0xFFFFFFFF
    return h


def scan_manifest(game_dir):
    """Scan .pyc files and build checksum manifest matching openbc-hash.exe."""
    manifest = {"rounds": []}

    # Checksum rounds (from BC checksum protocol):
    # Round 0: scripts/, App.pyc, non-recursive
    # Round 1: scripts/Custom/Multiplayer/, *.pyc, non-recursive
    # Round 2: scripts/ships/, *.pyc, recursive
    # Round 3: scripts/ships/Hardpoints/, *.pyc, recursive
    rounds = [
        {"dir": "scripts/", "filter": "App.pyc", "recursive": False},
        {"dir": "scripts/Custom/Multiplayer/", "filter": "*.pyc", "recursive": False},
        {"dir": "scripts/ships/", "filter": "*.pyc", "recursive": True},
        {"dir": "scripts/ships/Hardpoints/", "filter": "*.pyc", "recursive": True},
    ]

    for rnd_idx, rnd in enumerate(rounds):
        full_dir = os.path.join(game_dir, rnd["dir"].replace("/", os.sep))
        if not os.path.isdir(full_dir):
            print(f"  Round {rnd_idx}: {full_dir} not found, skipping",
                  file=sys.stderr)
            manifest["rounds"].append(None)
            continue

        # Compute dir_hash (leaf directory name only)
        dir_path = rnd["dir"].rstrip("/")
        leaf = dir_path.rsplit("/", 1)[-1] if "/" in dir_path else dir_path
        dir_h = string_hash(leaf)

        files = []
        filter_pat = rnd["filter"]

        def match_filter(fname):
            if filter_pat.startswith("*"):
                return fname.lower().endswith(filter_pat[1:].lower())
            return fname.lower() == filter_pat.lower()

        # Scan top-level files
        for fname in sorted(os.listdir(full_dir)):
            fpath = os.path.join(full_dir, fname)
            if os.path.isfile(fpath) and match_filter(fname):
                with open(fpath, "rb") as f:
                    data = f.read()
                files.append({
                    "name": fname,
                    "name_hash": string_hash(fname),
                    "content_hash": file_hash(data),
                })

        subdirs = []
        if rnd["recursive"]:
            for dname in sorted(os.listdir(full_dir)):
                dpath = os.path.join(full_dir, dname)
                if not os.path.isdir(dpath) or dname.startswith("."):
                    continue
                sub_files = []
                for fname in sorted(os.listdir(dpath)):
                    fpath = os.path.join(dpath, fname)
                    if os.path.isfile(fpath) and match_filter(fname):
                        with open(fpath, "rb") as f:
                            data = f.read()
                        sub_files.append({
                            "name": fname,
                            "name_hash": string_hash(fname),
                            "content_hash": file_hash(data),
                        })
                if sub_files:
                    subdirs.append({
                        "name": dname,
                        "name_hash": string_hash(dname),
                        "files": sub_files,
                    })

        round_data = {
            "index": rnd_idx,
            "directory": rnd["dir"],
            "filter": rnd["filter"],
            "recursive": rnd["recursive"],
            "dir_hash": dir_h,
            "files": files,
        }
        if subdirs:
            round_data["subdirs"] = subdirs

        total = len(files) + sum(len(sd["files"]) for sd in subdirs)
        manifest["rounds"].append(round_data)
        print(f"  Round {rnd_idx}: {rnd['dir']} -> {total} files, "
              f"dir_hash=0x{dir_h:08X}", file=sys.stderr)

    return manifest


def main():
    parser = argparse.ArgumentParser(description="Scrape BC reference scripts to JSON")
    parser.add_argument("scripts_dir", help="Path to BC scripts/ directory")
    parser.add_argument("-o", "--output", default=None,
                        help="Output JSON file (default: stdout)")
    parser.add_argument("--game-dir", default=None,
                        help="Path to BC game directory for manifest scanning")
    args = parser.parse_args()

    if not os.path.isdir(args.scripts_dir):
        print(f"Error: {args.scripts_dir} is not a directory", file=sys.stderr)
        sys.exit(1)

    print("Scraping ships...", file=sys.stderr)
    ships = scrape_ships(args.scripts_dir)
    print(f"Found {len(ships)} ships", file=sys.stderr)

    print("\nScraping projectiles...", file=sys.stderr)
    projectiles = scrape_projectiles(args.scripts_dir)
    print(f"Found {len(projectiles)} projectile types", file=sys.stderr)

    registry = {
        "version": "vanilla-1.1",
        "ships": ships,
        "projectiles": projectiles,
    }

    if args.game_dir:
        print("\nScanning manifest...", file=sys.stderr)
        registry["manifest"] = scan_manifest(args.game_dir)

    output = json.dumps(registry, indent=2)

    if args.output:
        os.makedirs(os.path.dirname(args.output) or ".", exist_ok=True)
        with open(args.output, "w") as f:
            f.write(output)
            f.write("\n")
        print(f"\nWritten to {args.output}", file=sys.stderr)
    else:
        print(output)


if __name__ == "__main__":
    main()
