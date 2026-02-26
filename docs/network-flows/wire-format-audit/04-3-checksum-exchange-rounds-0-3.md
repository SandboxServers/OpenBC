# 3. Checksum Exchange (Rounds 0-3)


### What OpenBC does
```
Round 0: dir="scripts/"          filter="App.pyc"       recursive=false
Round 1: dir="scripts/"          filter="Autoexec.pyc"   recursive=false
Round 2: dir="scripts/ships/"    filter="*.pyc"           recursive=true
Round 3: dir="scripts/mainmenu/" filter="*.pyc"           recursive=false
```

### What traces show
```
Round 0: dir="scripts/"          filter="App.pyc"       recursive=0  (27 bytes)
Round 1: dir="scripts/"          filter="Autoexec.pyc"   recursive=0  (32 bytes)
Round 2: dir="scripts/ships"     filter="*.pyc"           recursive=1  (30 bytes)
Round 3: dir="scripts/mainmenu"  filter="*.pyc"           recursive=0  (33 bytes)
```

### Verdict: MATCH (with trailing slash detail)
The directory strings in the trace do NOT have trailing slashes:
- `scripts/ships` not `scripts/ships/`
- `scripts/mainmenu` not `scripts/mainmenu/`

OpenBC's round definitions use trailing slashes (`scripts/ships/`, `scripts/mainmenu/`). The base directory `scripts/` appears both with a trailing slash in rounds 0-1. For rounds 2-3, the trailing slash **may affect the dir_hash** used for checksum validation. This should be verified.

**Wire format**: `[0x20][round:u8][dirLen:u16][dir:bytes][filterLen:u16][filter:bytes][recursive:bit]` -- matches.

The bit byte for recursive=true is `0x21` (from "pyc!" in hex: `70 79 63 21` -- the `21` after "pyc" is the bit-packed byte with count=0, bit0=1).

For recursive=false, the bit byte is `0x20` (count=0, bit0=0). Wait -- from the trace `20` appears at the end of non-recursive rounds. But `0x20` with count field = (0x20>>5)=1 would mean count=1, 1 bit, value=0. This doesn't match the OpenBC encoding either.

Actually wait, let me re-read. The last byte of round 0: `... 41 70 70 2E 70 79 63 20`. The `20` is the trailing byte = 0x20 = space character. But in the bit encoding, `0x20 = (1<<5) | 0 = count=1, value=0`. With the real TGBufferStream encoding, count=1 means 1 bit was written with value=0 (recursive=false). This is CORRECT for the real encoding.

With OpenBC's encoding for 1 bit (recursive=false): `count-1 = 0`, so byte = `(0<<5) | 0 = 0x00`.
With real encoding: count=1, byte = `(1<<5) | 0 = 0x20`.

**This confirms the bit-packing mismatch**: even for the checksum request's recursive flag, OpenBC encodes `0x00` while the real game encodes `0x20`. This corrupts the checksum request wire format.

---

