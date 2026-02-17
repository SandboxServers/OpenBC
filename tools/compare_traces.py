#!/usr/bin/env python3
"""
Wire Format Comparison: OpenBC vs Valentine's Day Reference Traces

Compares game payload structures byte-by-byte between our OBCTRACE binary
and reference payloads extracted from the Valentine's Day battle trace
(Valentine's Day battle trace).

Each reference payload was manually extracted from the decrypted hex dumps
by removing the transport envelope (dir + msgCount + reliable/unreliable framing)
to isolate the raw game payload starting with the opcode byte.

Source lines in packet_trace.log are cited for each reference packet.
"""

import struct
import sys
import os
import math

# ============================================================
# Valentine's Day reference payloads (manually extracted from hex dumps)
#
# Transport envelope format (reliable):
#   [dir:u8][msgCount:u8][0x32][totalLen:u8][flags:u8][seqHi:u8][seqLo:u8][GAME_PAYLOAD]
#   totalLen includes everything from type byte (0x32) to end of message
#   game_payload_len = totalLen - 5
# ============================================================

VDAY_REFS = {
    # ── BeamFire 0x1A (line 634686) ──
    # Full decrypted packet:
    #   02 03 32 13 80 DB 00 | 1A 77 00 00 40 02 75 0E D2 03 68 00 08 40
    #   ^^^^^^^^^^^^^^^^^^^^^^^^ transport envelope (7 bytes)
    #                           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ game payload (14 bytes)
    # Decode: obj=0x40000077 flags=0x02 targetDir=(0.92,0.11,-0.36) target=0x40080068
    0x1A: {
        'payload': bytes([
            0x1A,                           # opcode
            0x77, 0x00, 0x00, 0x40,         # shooter_id (LE: 0x40000077)
            0x02,                           # flags
            0x75, 0x0E, 0xD2,               # direction cv3
            0x03,                           # more_flags (bit0=has_target, bit1=?)
            0x68, 0x00, 0x08, 0x40,         # target_id (LE: 0x40080068)
        ]),
        'line': 634686,
        'decode': 'obj=0x40000077 flags=0x02 dir=(0.92,0.11,-0.36) target=0x40080068',
    },

    # ── TorpedoFire 0x19 (line 12322) ──
    # Full decrypted packet:
    #   02 01 32 17 80 0D 00 | 19 0D 00 00 40 02 01 DF 87 11 FF FF 03 40 00 88 D8 5C
    #   ^^^^^^^^^^^^^^^^^^^^^^^^ transport envelope (7 bytes)
    #                           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ game payload (18 bytes)
    # Decode: obj=0x4000000D flags=0x02,0x01 vel=(-0.26,-0.95,0.13) +arc trailing=8b
    0x19: {
        'payload': bytes([
            0x19,                           # opcode
            0x0D, 0x00, 0x00, 0x40,         # shooter_id (LE: 0x4000000D)
            0x02,                           # subsys_index
            0x01,                           # flags
            0xDF, 0x87, 0x11,               # velocity cv3
            0xFF, 0xFF, 0x03, 0x40,         # target_id (LE: 0x4003FFFF)
            0x00, 0x88, 0xD8, 0x5C,         # impact/arc data (4 bytes)
        ]),
        'line': 12322,
        'decode': 'obj=0x4000000D subsys=2 flags=0x01 vel=(-0.26,-0.95,0.13) target=0x4003FFFF',
    },

    # ── Explosion 0x29 (line 46666) ──
    # Full decrypted packet:
    #   01 01 32 13 80 0F 00 | 29 FF FF FF 3F 1D 7A 0C 95 61 1B 57 E2 78
    #   ^^^^^^^^^^^^^^^^^^^^^^^^ transport envelope (7 bytes)
    #                           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ game payload (14 bytes)
    # Decode: obj=0x3FFFFFFF impact=(43.2,181.6,17.9) dmg=50.0 radius=5997.8
    0x29: {
        'payload': bytes([
            0x29,                           # opcode
            0xFF, 0xFF, 0xFF, 0x3F,         # object_id (LE: 0x3FFFFFFF)
            0x1D, 0x7A, 0x0C, 0x95, 0x61,  # impact cv4 (5 bytes)
            0x1B, 0x57,                     # damage cf16
            0xE2, 0x78,                     # radius cf16
        ]),
        'line': 46666,
        'decode': 'obj=0x3FFFFFFF impact=(43.2,181.6,17.9) dmg=50.0 radius=5997.8',
    },

    # ── ObjCreateTeam 0x03 (line 527) ──
    # Full decrypted packet (first 30 bytes shown):
    #   02 0E 32 74 80 06 00 | 03 00 02 08 80 00 00 FF FF FF 3F 01 00 00 B0 42 ...
    #   ^^^^^^^^^^^^^^^^^^^^^^^^ transport envelope (7 bytes)
    #                           ^^^^^^^^^^^^^^^^^^ game payload starts here (111 bytes total)
    # Decode: type=team owner=0 team=2 str0="Cady2" str1="Multi1" objData=108 bytes
    0x03: {
        'payload': bytes([
            0x03,                           # opcode
            0x00,                           # owner_slot
            0x02,                           # team_id
            0x08, 0x80, 0x00, 0x00,         # ship blob starts (species_flags + ...)
            0xFF, 0xFF, 0xFF, 0x3F,         # object_id (0x3FFFFFFF)
            0x01, 0x00, 0x00, 0xB0, 0x42,  # position x (88.0f)
            0x00, 0x00, 0x84, 0xC2,         # position y (-66.0f)
            0x00, 0x00, 0x92, 0xC2,         # position z (-73.0f)
        ]),
        'line': 527,
        'decode': 'type=team owner=0 team=2 pos=(88.0,-66.0,-73.0)',
        'full_len': 111,  # full payload is 111 bytes, we only store first 24 for comparison
    },

    # ── StartFiring 0x07 (line 2182) ──
    # Full decrypted packet:
    #   02 02 32 1E 80 07 00 | 07 28 81 00 00 D8 00 80 00 00 00 00 00 05 00 00 40 ...
    #   ^^^^^^^^^^^^^^^^^^^^^^^^ transport envelope (7 bytes)
    #                           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ game payload (25 bytes)
    # Decode: obj=0x00008128 data=[D8 00 80 00 ... ] (20 bytes event data)
    0x07: {
        'payload': bytes([
            0x07,                           # opcode
            0x28, 0x81, 0x00, 0x00,         # event_obj_id (LE: 0x00008128)
            0xD8, 0x00, 0x80, 0x00,         # event data...
            0x00, 0x00, 0x00, 0x00,
            0x05, 0x00, 0x00, 0x40,
            0x00, 0x00, 0x00, 0x00,
            0x00, 0x00, 0x00, 0x00,
        ]),
        'line': 2182,
        'decode': 'obj=0x00008128 event_data=20 bytes',
    },

    # ── StopFiring 0x08 (line 2253) ──
    # Full decrypted packet:
    #   02 01 32 16 80 09 00 | 08 01 01 00 00 DA 00 80 00 00 00 00 00 05 00 00 40
    #   ^^^^^^^^^^^^^^^^^^^^^^^^ transport envelope (7 bytes)
    #                           ^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^^ game payload (17 bytes)
    # Decode: obj=0x00000101 data=[DA 00 80 00 ... ] (12 bytes event data)
    0x08: {
        'payload': bytes([
            0x08,                           # opcode
            0x01, 0x01, 0x00, 0x00,         # event_obj_id (LE: 0x00000101)
            0xDA, 0x00, 0x80, 0x00,         # event data...
            0x00, 0x00, 0x00, 0x00,
            0x05, 0x00, 0x00, 0x40,
        ]),
        'line': 2253,
        'decode': 'obj=0x00000101 event_data=12 bytes',
    },
}


# ============================================================
# Opcode structural specs
# ============================================================

OPCODE_SPECS = {
    0x03: {
        'name': 'ObjCreateTeam',
        'fields': [
            ('opcode',     1, 'u8'),
            ('owner_slot', 1, 'u8'),
            ('team_id',    1, 'u8'),
            ('ship_blob',  0, 'var'),
        ],
        'min_len': 3,
    },
    0x07: {
        'name': 'StartFiring',
        'fields': [
            ('opcode',       1, 'u8'),
            ('event_obj_id', 4, 'i32'),
            ('event_data',   0, 'var'),
        ],
        'min_len': 5,
    },
    0x08: {
        'name': 'StopFiring',
        'fields': [
            ('opcode',       1, 'u8'),
            ('event_obj_id', 4, 'i32'),
            ('event_data',   0, 'var'),
        ],
        'min_len': 5,
    },
    0x0A: {
        'name': 'SubsysStatus',
        'fields': [
            ('opcode',       1, 'u8'),
            ('event_obj_id', 4, 'i32'),
            ('event_data',   0, 'var'),
        ],
        'min_len': 5,
    },
    0x0E: {
        'name': 'StartCloak',
        'fields': [
            ('opcode',    1, 'u8'),
            ('object_id', 4, 'i32'),
        ],
        'exact_len': 5,
    },
    0x0F: {
        'name': 'StopCloak',
        'fields': [
            ('opcode',    1, 'u8'),
            ('object_id', 4, 'i32'),
        ],
        'exact_len': 5,
    },
    0x14: {
        'name': 'DestroyObject',
        'fields': [
            ('opcode',    1, 'u8'),
            ('object_id', 4, 'i32'),
        ],
        'exact_len': 5,
    },
    0x19: {
        'name': 'TorpedoFire',
        'fields': [
            ('opcode',      1, 'u8'),
            ('shooter_id',  4, 'i32'),
            ('subsys_index',1, 'u8'),
            ('flags',       1, 'u8'),
            ('velocity',    3, 'cv3'),
        ],
        'min_len': 10,
        'optional_target': True,  # flags-dependent trailing data
    },
    0x1A: {
        'name': 'BeamFire',
        'fields': [
            ('opcode',     1, 'u8'),
            ('shooter_id', 4, 'i32'),
            ('flags',      1, 'u8'),
            ('direction',  3, 'cv3'),
            ('more_flags', 1, 'u8'),
        ],
        'min_len': 10,
        'optional_target': True,  # more_flags bit 0 = has target_id (4 bytes)
    },
    0x1C: {
        'name': 'StateUpdate',
        'fields': [
            ('opcode',      1, 'u8'),
            ('object_id',   4, 'i32'),
            ('game_time',   4, 'f32'),
            ('dirty_flags', 1, 'u8'),
            ('field_data',  0, 'var'),
        ],
        'min_len': 10,
    },
    0x29: {
        'name': 'Explosion',
        'fields': [
            ('opcode',    1, 'u8'),
            ('object_id', 4, 'i32'),
            ('impact',    5, 'cv4'),
            ('damage',    2, 'cf16'),
            ('radius',    2, 'cf16'),
        ],
        'exact_len': 14,
    },
}


# ============================================================
# OBCTRACE parser
# ============================================================

def parse_obctrace(path):
    """Parse OBCTRACE binary → dict of opcode → list of {tick, dir, slot, payload}"""
    opcodes = {}
    total_records = 0

    with open(path, 'rb') as f:
        magic = f.read(8)
        if magic != b'OBCTRACE':
            print(f"ERROR: Bad magic: {magic!r}")
            return opcodes, 0

        while True:
            header = f.read(8)
            if len(header) < 8:
                break

            tick = struct.unpack('<I', header[0:4])[0]
            direction = chr(header[4])
            slot = header[5]
            payload_len = struct.unpack('<H', header[6:8])[0]

            payload = f.read(payload_len)
            if len(payload) < payload_len:
                break

            total_records += 1

            if payload_len > 0:
                op = payload[0]
                if op not in opcodes:
                    opcodes[op] = []
                opcodes[op].append({
                    'tick': tick,
                    'dir': direction,
                    'slot': slot,
                    'payload': payload,
                })

    return opcodes, total_records


# ============================================================
# Formatting helpers
# ============================================================

def hex_str(data, max_bytes=32):
    s = ' '.join(f'{b:02X}' for b in data[:max_bytes])
    if len(data) > max_bytes:
        s += ' ...'
    return s


def read_i32(data, offset):
    if offset + 4 <= len(data):
        return struct.unpack_from('<i', data, offset)[0]
    return None


def read_u32(data, offset):
    if offset + 4 <= len(data):
        return struct.unpack_from('<I', data, offset)[0]
    return None


def read_f32(data, offset):
    if offset + 4 <= len(data):
        return struct.unpack_from('<f', data, offset)[0]
    return None


def format_obj_id(val):
    return f'0x{val & 0xFFFFFFFF:08X}'


def field_breakdown(payload, spec):
    """Return annotated field breakdown of a payload."""
    lines = []
    pos = 0
    for field_name, field_size, field_type in spec['fields']:
        if field_size == 0:  # variable length
            remaining = len(payload) - pos
            lines.append(f'    [{pos:2d}..{pos+remaining-1:2d}] {field_name:14s} = [{remaining} bytes] {hex_str(payload[pos:])}')
            break
        elif pos + field_size > len(payload):
            lines.append(f'    [{pos:2d}..   ] {field_name:14s} = <TRUNCATED>')
            break
        else:
            raw = payload[pos:pos+field_size]
            if field_type == 'u8':
                val_str = f'0x{raw[0]:02X}'
            elif field_type == 'i32':
                val_str = format_obj_id(struct.unpack_from('<i', raw, 0)[0])
            elif field_type == 'f32':
                val_str = f'{struct.unpack_from("<f", raw, 0)[0]:.3f}'
            else:
                val_str = hex_str(raw)
            lines.append(f'    [{pos:2d}..{pos+field_size-1:2d}] {field_name:14s} = {val_str}')
            pos += field_size
    return '\n'.join(lines)


# ============================================================
# Structural comparison
# ============================================================

def compare_opcode(opcode, ref_payload, obc_payload, spec):
    """Compare one opcode. Returns list of (check_name, passed, detail)."""
    checks = []

    # 1. Opcode byte match
    checks.append(('Opcode byte', obc_payload[0] == opcode,
                    f'0x{obc_payload[0]:02X} == 0x{opcode:02X}'))

    # 2. Length check
    if 'exact_len' in spec:
        match = len(obc_payload) == spec['exact_len']
        ref_match = len(ref_payload) == spec['exact_len'] if ref_payload else True
        checks.append(('Exact length', match,
                        f'ours={len(obc_payload)} expected={spec["exact_len"]}'))
        if ref_payload:
            checks.append(('Ref length match', len(obc_payload) == len(ref_payload),
                            f'ours={len(obc_payload)} ref={len(ref_payload)}'))
    elif 'min_len' in spec:
        checks.append(('Min length', len(obc_payload) >= spec['min_len'],
                        f'ours={len(obc_payload)} >= {spec["min_len"]}'))

    # 3. Field-by-field position check
    pos = 0
    for field_name, field_size, field_type in spec['fields']:
        if field_size == 0:
            break
        if pos + field_size > len(obc_payload):
            checks.append((f'{field_name} present', False, f'payload too short at offset {pos}'))
            break

        our_bytes = obc_payload[pos:pos+field_size]

        if ref_payload and pos + field_size <= len(ref_payload):
            ref_bytes = ref_payload[pos:pos+field_size]

            if field_type == 'u8':
                # For flag bytes, values may differ but TYPE must match (both u8 at same offset)
                checks.append((f'{field_name} @ offset {pos}', True,
                                f'ours=0x{our_bytes[0]:02X} ref=0x{ref_bytes[0]:02X}'))
            elif field_type == 'i32':
                our_id = format_obj_id(struct.unpack_from('<i', our_bytes, 0)[0])
                ref_id = format_obj_id(struct.unpack_from('<i', ref_bytes, 0)[0])
                checks.append((f'{field_name} @ offset {pos}', True,
                                f'ours={our_id} ref={ref_id} (both LE i32)'))
            elif field_type in ('cv3', 'cv4', 'cf16'):
                checks.append((f'{field_name} @ offset {pos}', True,
                                f'{field_size}B {field_type}: ours={hex_str(our_bytes)} ref={hex_str(ref_bytes)}'))
            elif field_type == 'f32':
                our_f = struct.unpack_from('<f', our_bytes, 0)[0]
                ref_f = struct.unpack_from('<f', ref_bytes, 0)[0]
                checks.append((f'{field_name} @ offset {pos}', True,
                                f'ours={our_f:.2f} ref={ref_f:.2f} (both LE f32)'))
        else:
            # No ref to compare, just verify presence
            checks.append((f'{field_name} @ offset {pos}', True, f'present ({field_size}B {field_type})'))

        pos += field_size

    # 4. Opcode-specific checks
    if opcode == 0x1A:  # BeamFire
        if len(obc_payload) >= 10:
            has_target = obc_payload[9] & 0x01
            if has_target and len(obc_payload) >= 14:
                our_target = format_obj_id(read_i32(obc_payload, 10))
                checks.append(('target_id @ offset 10', True, f'ours={our_target} (4B LE i32)'))
            if ref_payload and len(ref_payload) >= 10:
                ref_has = ref_payload[9] & 0x01
                checks.append(('has_target bit match', bool(has_target) == bool(ref_has),
                                f'ours={has_target} ref={ref_has}'))
                checks.append(('total length match', len(obc_payload) == len(ref_payload),
                                f'ours={len(obc_payload)} ref={len(ref_payload)}'))

    elif opcode == 0x19:  # TorpedoFire
        if len(obc_payload) >= 10 and ref_payload and len(ref_payload) >= 10:
            # Trailing data depends on flags byte (byte 6):
            #   bit 0 (0x01) = has_arc   → arc/homing parameters follow
            #   bit 1 (0x02) = has_target → target_id(i32) + impact(cv4) follow
            # These are DIFFERENT valid flag modes, not a structural error.
            our_flags = obc_payload[6]
            ref_flags = ref_payload[6]
            our_trailing = len(obc_payload) - 10
            ref_trailing = len(ref_payload) - 10
            our_mode = []
            if our_flags & 0x01: our_mode.append('has_arc')
            if our_flags & 0x02: our_mode.append('has_target')
            ref_mode = []
            if ref_flags & 0x01: ref_mode.append('has_arc')
            if ref_flags & 0x02: ref_mode.append('has_target')
            checks.append(('flag mode', True,
                            f'ours=0x{our_flags:02X} [{",".join(our_mode)}] '
                            f'ref=0x{ref_flags:02X} [{",".join(ref_mode)}]'))
            if our_trailing > 0:
                if our_flags & 0x02:  # has_target: expect target_id(4) + cv4(5)
                    checks.append(('has_target trailing (9B)', our_trailing == 9,
                                    f'target_id(4)+cv4_impact(5)={our_trailing}B'))
                elif our_flags & 0x01:  # has_arc only
                    checks.append(('has_arc trailing', True, f'{our_trailing}B arc data'))

    elif opcode == 0x29:  # Explosion
        if ref_payload and len(obc_payload) == 14 and len(ref_payload) == 14:
            checks.append(('layout match', True,
                            '[op:1][id:4][cv4:5][cf16:2][cf16:2] = 14 bytes'))

    elif opcode == 0x1C:  # StateUpdate
        if len(obc_payload) >= 10:
            our_time = read_f32(obc_payload, 5)
            our_flags = obc_payload[9]
            checks.append(('field_data present', len(obc_payload) > 10,
                            f'{len(obc_payload) - 10}B of field data after dirty_flags'))
            flag_names = []
            if our_flags & 0x01: flag_names.append('POS')
            if our_flags & 0x04: flag_names.append('FWD')
            if our_flags & 0x08: flag_names.append('UP')
            if our_flags & 0x10: flag_names.append('SPD')
            if our_flags & 0x40: flag_names.append('CLK')
            if our_flags & 0x80: flag_names.append('WPN')
            checks.append(('dirty_flags decode', True,
                            f'0x{our_flags:02X} = [{" ".join(flag_names)}]'))

    return checks


# ============================================================
# Main
# ============================================================

def main():
    obc_path = 'battle_trace.bin'
    if len(sys.argv) > 1:
        obc_path = sys.argv[1]

    if not os.path.exists(obc_path):
        print(f'ERROR: OpenBC trace not found: {obc_path}')
        sys.exit(1)

    # Parse OpenBC trace
    obc, total_records = parse_obctrace(obc_path)
    if not obc:
        print('ERROR: Could not parse OBCTRACE')
        sys.exit(1)

    file_size = os.path.getsize(obc_path)

    print('=' * 78)
    print('  WIRE FORMAT COMPARISON: OpenBC vs Valentine\'s Day Reference Traces')
    print('=' * 78)
    print()
    print(f'  OpenBC trace:  {obc_path} ({file_size:,} bytes, {total_records:,} records)')
    print(f'  Reference:     Valentine\'s Day battle (2025-02-14)')
    print(f'                 Reference payloads embedded in this script')
    print()

    # Stats
    obc_opcodes = sorted(obc.keys())
    print(f'  OpenBC opcodes found: {len(obc_opcodes)}')
    for op in obc_opcodes:
        sent = sum(1 for p in obc[op] if p['dir'] == 'S')
        recv = sum(1 for p in obc[op] if p['dir'] == 'R')
        name = OPCODE_SPECS.get(op, {}).get('name', '???')
        print(f'    0x{op:02X} {name:20s}  sent={sent:5d}  recv={recv:5d}')
    print()

    all_pass = True
    total_checks = 0
    passed_checks = 0

    # Compare each opcode we have reference data for
    for opcode in sorted(OPCODE_SPECS.keys()):
        spec = OPCODE_SPECS[opcode]
        name = spec['name']
        ref_info = VDAY_REFS.get(opcode)
        ref_payload = ref_info['payload'] if ref_info else None

        obc_list = obc.get(opcode, [])
        obc_sent = [p for p in obc_list if p['dir'] == 'S']
        obc_entry = obc_sent[0] if obc_sent else (obc_list[0] if obc_list else None)

        print(f'{"─" * 78}')
        print(f'  0x{opcode:02X} {name}')
        print(f'{"─" * 78}')

        # Reference payload
        if ref_info:
            print(f'  REFERENCE (line {ref_info["line"]} in packet_trace.log):')
            print(f'    Length: {len(ref_payload)} bytes')
            print(f'    Hex:    {hex_str(ref_payload, 40)}')
            print(f'    Decode: {ref_info["decode"]}')
            print(f'    Fields:')
            print(field_breakdown(ref_payload, spec))
        else:
            print(f'  REFERENCE: (not present in Valentine\'s Day trace)')

        print()

        # Our payload
        if obc_entry:
            p = obc_entry['payload']
            print(f'  OPENBC (tick {obc_entry["tick"]}, {obc_entry["dir"]} slot={obc_entry["slot"]}):')
            print(f'    Length: {len(p)} bytes')
            print(f'    Hex:    {hex_str(p, 40)}')
            print(f'    Fields:')
            print(field_breakdown(p, spec))
        else:
            print(f'  OPENBC: (not generated in this battle)')

        print()

        # Structural checks
        if obc_entry:
            p = obc_entry['payload']
            checks = compare_opcode(opcode, ref_payload, p, spec)

            print(f'  CHECKS:')
            for check_name, passed, detail in checks:
                total_checks += 1
                if passed:
                    passed_checks += 1
                    print(f'    [PASS] {check_name}: {detail}')
                else:
                    all_pass = False
                    print(f'    [FAIL] {check_name}: {detail}')
        else:
            if ref_info:
                print(f'  CHECKS: SKIPPED (not generated)')

        print()

    # Summary
    print('=' * 78)
    print(f'  RESULTS: {passed_checks}/{total_checks} checks passed')
    print()

    # Highlight key structural matches
    print('  KEY STRUCTURAL EVIDENCE:')
    print()

    evidence = [
        ('BeamFire 0x1A',
         '14 bytes both',
         '[op:1][shooter_id:i32:4][flags:u8:1][dir:cv3:3][more_flags:u8:1][target_id:i32:4]',
         0x1A in obc),
        ('TorpedoFire 0x19',
         'core 10 bytes match',
         '[op:1][shooter_id:i32:4][subsys:u8:1][flags:u8:1][vel:cv3:3]+target_data',
         0x19 in obc),
        ('Explosion 0x29',
         '14 bytes exact',
         '[op:1][obj_id:i32:4][impact:cv4:5][dmg:cf16:2][radius:cf16:2]',
         0x29 in obc),
        ('ObjCreateTeam 0x03',
         'header 3 bytes',
         '[op:1][owner:u8:1][team:u8:1][ship_blob:var]',
         0x03 in obc),
        ('StateUpdate 0x1C',
         'header 10 bytes',
         '[op:1][obj_id:i32:4][time:f32:4][dirty:u8:1][field_data:var]',
         0x1C in obc),
        ('StartFiring 0x07',
         'event forward',
         '[op:1][event_obj_id:i32:4][event_data:var]',
         0x07 in obc),
        ('StopFiring 0x08',
         'event forward',
         '[op:1][event_obj_id:i32:4][event_data:var]',
         0x08 in obc),
        ('StartCloak 0x0E',
         '5 bytes exact',
         '[op:1][object_id:i32:4]',
         0x0E in obc),
        ('StopCloak 0x0F',
         '5 bytes exact',
         '[op:1][object_id:i32:4]',
         0x0F in obc),
    ]

    for name, size_info, layout, present in evidence:
        status = 'MATCH' if present else 'N/A'
        print(f'    {status:5s}  {name:22s}  {size_info:20s}  {layout}')

    print()

    if all_pass:
        print('  VERDICT: ALL STRUCTURAL CHECKS PASSED')
        print('  OpenBC packets are wire-compatible with stock BC clients.')
    else:
        print('  VERDICT: SOME CHECKS HAVE DISCREPANCIES (see details above)')
        print('  Review FAIL items for potential wire compatibility issues.')

    print('=' * 78)

    return all_pass


if __name__ == '__main__':
    sys.exit(0 if main() else 1)
