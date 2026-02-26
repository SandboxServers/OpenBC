# Wire Format


### ChecksumReq (Server -> Client, opcode `0x20`)

Sent inside a reliable transport wrapper.

```
Offset  Type    Field
------  ------  -----
0       u8      opcode = 0x20
1       u8      round_index (0x00, 0x01, 0x02, 0x03, or 0xFF)
2       u16le   directory_length
4       bytes   directory_name (NOT null-terminated)
+0      u16le   filter_length
+2      bytes   filter_name (NOT null-terminated)
+0      bitbyte recursive_flag (see Bit-Packed Boolean below)
```

### ChecksumResp (Client -> Server, opcode `0x21`)

The client responds with opcode `0x21` containing the round index and hash data.

```
Offset  Type    Field
------  ------  -----
0       u8      opcode = 0x21
1       u8      round_index (echoes the request's round_index)
2       u32le   ref_hash (ONLY round 0 -- StringHash of gamever "60")
6       u32le   dir_hash (hash of the directory name)
6/10+   var     file_tree (see Checksum Response Tree Format below)

Round 0:  [0x21][round][ref_hash:u32][dir_hash:u32][file_tree...]  (10-byte header)
Other:    [0x21][round][dir_hash:u32][file_tree...]                 (6-byte header)
```

**Important**: Only round 0 includes `ref_hash`. Rounds 1, 2, 3, and 0xFF start
directly with `dir_hash` at offset 2. Verified by live client packet analysis.

### Checksum Response Tree Format

The hash data after the directory hash follows a recursive tree structure. For **non-recursive rounds** (0, 1, 3), it contains a flat list of files. For **recursive rounds** (2, 0xFF), it additionally contains subdirectories, each with their own file list.

**File tree** (self-describing, same format at all nesting levels):
```
[file_count:u16le]
  repeated file_count times:
    [name_hash:u32le]       -- hash of the filename
    [content_hash:u32le]    -- hash of the file contents
[subdir_count:u8]             -- ALWAYS present (u8, NOT u16)
  ALL subdir name hashes listed first:
    [subdir_name_hash_0:u32le]
    [subdir_name_hash_1:u32le]
    ...
  THEN each subdir's tree (same recursive format):
    [tree_0]
    [tree_1]
    ...
```

The tree format is self-describing: if `subdir_count` is 0, the node is a leaf.
Non-recursive rounds (0, 1, 3) always have `subdir_count=0` (one trailing byte).
Recursive rounds (2, 0xFF) have the full recursive structure.

**Important**: Name hashes are listed contiguously BEFORE the trees, not interleaved
with them. Each subtree recursively uses this same format.

**Example decode** -- Round 0 response (packet #7):
```
Hash data (after opcode + index):
43 E2 0C 7E  -- ref_hash = 0x7E0CE243 (StringHash("60"))
2F CB AF 4D  -- dir_hash = 0x4DAFCB2F (hash of "scripts/")
01 00        -- file_count = 1
77 B6 3E 37  -- files[0].name_hash = 0x373EB677 (StringHash("App.pyc"))
A7 A0 F8 00  -- files[0].content_hash (FileHash of App.pyc)
00           -- subdir_count = 0
```

Round 0 has 1 file (App.pyc). Round 1 has 1 file (Autoexec.pyc). Round 2 is large (~400 bytes) because it contains all ship `.pyc` files plus subdirectories. Round 0xFF is similarly large for `Scripts/Multiplayer` with subdirectories.

**Hash algorithm**: Both `name_hash` and `content_hash` use a Pearson-based hash with four 256-byte substitution tables forming Mutually Orthogonal Latin Squares (MOLS). `StringHash` operates on the lowercased filename string; `FileHash` operates on the raw file bytes, skipping bytes 4-7 in `.pyc` files (the compilation timestamp). Verified test vector: `StringHash("60") == 0x7E0CE243`.

The `ref_hash` is `StringHash("60")` = `0x7E0CE243`, corresponding to the game version string. It appears only in round 0 and acts as a protocol version marker. All other rounds omit it.

### Bit-Packed Boolean Encoding

The recursive flag is encoded as a single bit using a compact bit-packing scheme:

```
Byte layout: [count_minus_1:3][padding:4][bit0:1]

For a single boolean:
  false = 0x20  (count=1 in upper 3 bits, bit0=0)
  true  = 0x21  (count=1 in upper 3 bits, bit0=1)
```

This is NOT a plain byte — it is a bit-packed value. The upper 3 bits encode how many boolean values are packed into this byte (value 1 = one bit stored). The lowest bit holds the actual boolean. Implementations that write a plain `0x00`/`0x01` byte here will break the client's stream parser, causing all subsequent reads to be misaligned.

---

