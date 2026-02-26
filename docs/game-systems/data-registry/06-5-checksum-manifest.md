# 5. Checksum Manifest


### 5.1 Source

Verified from wire format specification and protocol traces.

### 5.2 Checksum Directory Rounds

The server sends 4 sequential checksum requests (opcode 0x20). Each round validates a specific directory scope:

| Round | Directory | Filter | Recursive | Notes |
|-------|-----------|--------|-----------|-------|
| 0 | `scripts/` | `App.pyc` | No | Single file |
| 1 | `scripts/` | `Autoexec.pyc` | No | Single file |
| 2 | `scripts/ships/` | `*.pyc` | Yes | All ship + hardpoint scripts |
| 3 | `scripts/mainmenu/` | `*.pyc` | No | Main menu scripts only |

**`scripts/Custom/` is EXEMPT from all checksum validation.** This is where mods install (DedicatedServer.py, Foundation Technologies, NanoFX, etc.).

### 5.3 Hash Algorithms

**StringHash**: 4-lane Pearson hash using four 256-byte substitution tables (1,024 bytes total, extracted via hash manifest tool).

```c
uint32_t StringHash(const char *str) {
    uint32_t h0 = 0, h1 = 0, h2 = 0, h3 = 0;
    while (*str) {
        uint8_t c = (uint8_t)*str++;
        h0 = TABLE_0[c ^ h0];
        h1 = TABLE_1[c ^ h1];
        h2 = TABLE_2[c ^ h2];
        h3 = TABLE_3[c ^ h3];
    }
    return (h0 << 24) | (h1 << 16) | (h2 << 8) | h3;
}
```

Used for: directory name hashing, filename hashing, version string hashing.

**FileHash**: Rotate-XOR over file contents.

```c
uint32_t FileHash(const uint8_t *data, size_t len) {
    uint32_t hash = 0;
    const uint32_t *dwords = (const uint32_t *)data;
    size_t count = len / 4;
    for (size_t i = 0; i < count; i++) {
        if (i == 1) continue;  // Skip DWORD index 1 (bytes 4-7 = .pyc timestamp)
        hash ^= dwords[i];
        hash = (hash << 1) | (hash >> 31);  // ROL 1
    }
    // Remaining bytes (len % 4): MOVSX sign-extension before XOR
    size_t remainder = len % 4;
    if (remainder > 0) {
        const uint8_t *tail = data + (count * 4);
        for (size_t i = 0; i < remainder; i++) {
            int32_t extended = (int8_t)tail[i];  // MOVSX
            hash ^= (uint32_t)extended;
            hash = (hash << 1) | (hash >> 31);
        }
    }
    return hash;
}
```

Deliberately skips bytes 4-7 (.pyc modification timestamp) so that the same bytecode produces the same hash regardless of compile time.

### 5.4 Version String Gate

- Version string: `"60"`
- Version hash: `StringHash("60") = 0x7E0CE243`
- Checked in the first checksum round (index 0). Version mismatch causes immediate rejection via opcode 0x23.

### 5.5 Hash Manifest JSON Schema

```json
{
  "meta": {
    "name": "Star Trek: Bridge Commander 1.1",
    "description": "Vanilla BC 1.1 (GOG release)",
    "generated": "2026-02-15T00:00:00Z",
    "generator_version": "1.0.0",
    "game_version": "60"
  },
  "version_string": "60",
  "version_string_hash": "0x7E0CE243",
  "directories": [
    {
      "index": 0,
      "path": "scripts/",
      "filter": "App.pyc",
      "recursive": false,
      "dir_name_hash": "0x...",
      "files": [
        {
          "filename": "App.pyc",
          "name_hash": "0x...",
          "content_hash": "0x..."
        }
      ],
      "subdirs": []
    },
    {
      "index": 1,
      "path": "scripts/",
      "filter": "Autoexec.pyc",
      "recursive": false,
      "dir_name_hash": "0x...",
      "files": [
        {
          "filename": "Autoexec.pyc",
          "name_hash": "0x...",
          "content_hash": "0x..."
        }
      ],
      "subdirs": []
    },
    {
      "index": 2,
      "path": "scripts/ships/",
      "filter": "*.pyc",
      "recursive": true,
      "dir_name_hash": "0x...",
      "files": [],
      "subdirs": [
        {
          "name": "Hardpoints",
          "name_hash": "0x...",
          "files": [
            {
              "filename": "sovereign.pyc",
              "name_hash": "0x...",
              "content_hash": "0x..."
            }
          ]
        }
      ]
    },
    {
      "index": 3,
      "path": "scripts/mainmenu/",
      "filter": "*.pyc",
      "recursive": false,
      "dir_name_hash": "0x...",
      "files": [
        {
          "filename": "mainmenu.pyc",
          "name_hash": "0x...",
          "content_hash": "0x..."
        }
      ],
      "subdirs": []
    }
  ]
}
```

### 5.6 Validation Flow

1. Client connects. Server sends 4x opcode 0x20 (checksum request), one per directory.
2. Client hashes its local files, sends 4x opcode 0x21 (checksum response trees).
3. Server walks each response tree against the active manifest(s):
   - Version string hash checked first (index 0 only) -- reject on mismatch (opcode 0x23)
   - Directory name hashes compared
   - File name hashes compared (sorted order)
   - File content hashes compared
4. All match any active manifest --> fire ChecksumComplete, send Settings (0x00) + GameInit (0x01)
5. Mismatch --> send opcode 0x22 (file mismatch) or 0x23 (version mismatch) with failing filename

---

