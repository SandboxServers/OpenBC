#!/usr/bin/env python3
"""Decode a checksum response payload from hex dump."""

import struct
import sys

# Full 268-byte round 0xFF payload from server log
HEX = """
21 FF 3F D1 94 87 09 00 2D 71 11 2C 8F 73 CE 86
85 C0 A4 67 82 A7 61 59 29 A7 25 08 C7 33 FF AC
37 8D 1F CB 31 9C 5B A5 48 07 AF 44 3E FA 19 DA
E9 30 41 1F 71 50 57 F5 E5 8A DE C5 74 0E DC 09
7B F2 53 FC F2 AD 19 8D A2 9F DD E9 7D 1F 45 6F
01 A5 2B C8 83 02 00 86 26 2E 71 C7 3D 39 B2 A2
9F DD E9 80 8D A7 9B 04 BC 3A D6 88 08 A2 7D 58
51 38 ED 72 DA E8 F2 16 04 00 38 D9 5B 41 3A 08
A7 DE 6C 51 40 02 E9 8E 33 C1 8B 81 59 63 CB 53
47 5C A2 9F DD E9 FF 89 53 4D 00 04 00 37 B0 EA
88 9E F3 82 E6 E7 CD 32 EA 65 8B 14 1E 94 E2 FE
A6 FD 04 26 41 A2 9F DD E9 FF 89 55 4D 00 04 00
15 64 FC A4 CF E4 8A 8F 34 F0 F3 58 18 7A D5 35
DD 3E 42 13 EF C9 06 4A A2 9F DD E9 FF 89 57 4D
00 05 00 1C 4B F6 6D B1 54 B3 A3 3D 4F 0B 9D 0B
6A 13 66 0B 23 27 7B 83 67 C4 70 E2 43 D4 67 97
6B 06 28 A2 9F DD E9 FF 89 5B 4D 00
"""

data = bytes.fromhex(HEX.replace('\n', ' ').strip())
print(f"Total payload: {len(data)} bytes\n")

pos = 0

def read_u8():
    global pos
    v = data[pos]
    pos += 1
    return v

def read_u16():
    global pos
    v = struct.unpack_from('<H', data, pos)[0]
    pos += 2
    return v

def read_u32():
    global pos
    v = struct.unpack_from('<I', data, pos)[0]
    pos += 4
    return v

def peek(n=4):
    return ' '.join(f'{b:02X}' for b in data[pos:pos+n])

# Header
opcode = read_u8()
round_idx = read_u8()
print(f"opcode=0x{opcode:02X} round=0x{round_idx:02X}")

# dir_hash (no ref_hash for non-zero rounds)
if round_idx == 0:
    ref_hash = read_u32()
    print(f"ref_hash=0x{ref_hash:08X}")
dir_hash = read_u32()
print(f"dir_hash=0x{dir_hash:08X}")

print(f"\n--- Hypothesis A: file entries = 8 bytes, subdir_count = u16 ---")
# Standard format: [fc:u16][files*8][sc:u16][subdirs]
saved = pos
fc = read_u16()
print(f"file_count={fc} (pos {saved})")
for i in range(fc):
    nh = read_u32()
    ch = read_u32()
    print(f"  file[{i}] name=0x{nh:08X} content=0x{ch:08X}")
print(f"  -- after files: pos={pos}, peek: {peek(8)}")
sc_raw = read_u16()
print(f"subdir_count={sc_raw} (0x{sc_raw:04X}) -- {'INVALID' if sc_raw > 100 else 'ok'}")

print(f"\n--- Hypothesis B: subdir_count = u8 (not u16) ---")
pos = saved
fc = read_u16()
print(f"file_count={fc}")
for i in range(fc):
    nh = read_u32()
    ch = read_u32()
sc_u8 = read_u8()
print(f"subdir_count(u8)={sc_u8} at pos {pos-1}, peek after: {peek(8)}")

if sc_u8 > 0 and sc_u8 < 50:
    print(f"\n  Trying {sc_u8} subdirectories (recursive tree)...")
    def parse_tree(indent=2):
        """Parse: [fc:u16][files*8][sc:u8][subdirs*(name_hash:u32 + tree)]"""
        pfx = ' ' * indent
        fc = read_u16()
        print(f"{pfx}file_count={fc} (pos {pos-2})")
        for i in range(min(fc, 50)):
            nh = read_u32()
            ch = read_u32()
            print(f"{pfx}  file[{i}] name=0x{nh:08X} content=0x{ch:08X}")
        sc = read_u8()
        print(f"{pfx}subdir_count(u8)={sc} (pos {pos-1})")
        for i in range(min(sc, 20)):
            if pos >= len(data):
                print(f"{pfx}  ERROR: ran out of data at subdir {i}")
                return
            name = read_u32()
            print(f"{pfx}  subdir[{i}] name=0x{name:08X} (pos {pos-4})")
            parse_tree(indent + 4)

    try:
        for i in range(sc_u8):
            name = read_u32()
            print(f"  subdir[{i}] name=0x{name:08X} (pos {pos-4})")
            parse_tree(4)

        if pos == len(data):
            print(f"\n  *** PERFECT PARSE: consumed exactly {pos}/{len(data)} bytes ***")
        else:
            print(f"\n  Consumed {pos}/{len(data)} bytes ({len(data)-pos} remaining)")
    except Exception as e:
        print(f"\n  ERROR at pos {pos}: {e}")

print(f"\n--- Hypothesis D: u8 subdir_count, names-first then trees ---")
pos = 6  # reset to file_count

def parse_tree_d(indent=0):
    """Parse: [fc:u16][files*8][sc:u8][name0:u32..nameN:u32][tree0][tree1]..."""
    global pos
    pfx = ' ' * indent
    fc = read_u16()
    print(f"{pfx}file_count={fc} (pos {pos-2})")
    for i in range(min(fc, 100)):
        nh = read_u32()
        ch = read_u32()
        print(f"{pfx}  file[{i}] name=0x{nh:08X} content=0x{ch:08X}")
    if pos >= len(data):
        print(f"{pfx}  (end of data, no subdir_count)")
        return
    sc = read_u8()
    print(f"{pfx}subdir_count(u8)={sc} (pos {pos-1})")
    if sc == 0:
        return
    # Read ALL name hashes first
    names = []
    for i in range(sc):
        n = read_u32()
        names.append(n)
        print(f"{pfx}  subdir_name[{i}] = 0x{n:08X} (pos {pos-4})")
    # Then parse trees sequentially
    for i in range(sc):
        print(f"{pfx}  --- subdir[{i}] 0x{names[i]:08X} tree at pos {pos} ---")
        parse_tree_d(indent + 4)

try:
    parse_tree_d(0)
    if pos == len(data):
        print(f"\n*** PERFECT PARSE: consumed exactly {pos}/{len(data)} bytes ***")
    else:
        print(f"\nConsumed {pos}/{len(data)} bytes ({len(data)-pos} remaining)")
except Exception as e:
    print(f"\nERROR at pos {pos}: {e}")
