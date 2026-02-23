# OpenBC Test Suite

19 test suites, 290 tests, 1,369 assertions. All tests cross-compile from WSL2 to Win32 and run natively.

## Running Tests

```
make test                            # all 19 suites
./build/tests/test_checksum.exe      # single suite (verbose)
```

## Test Suites

| Suite | Tests | Asserts | Covers |
|-------|------:|--------:|--------|
| `test_protocol.c` | 80 | 454 | Wire cipher, buffer codec, CF16/CV3/CV4 compression, handshake builders |
| `test_registry.c` | 49 | 197 | Ship data registry: 16 ships, 15 projectiles, subsystem lookup, power |
| `test_gamespy.c` | 32 | 126 | GameSpy query/response, master server, LAN broadcast |
| `test_game_builders.c` | 26 | 119 | Packet builders: settings, gameinit, mission, boot, UI, state updates |
| `test_game_events.c` | 22 | 54 | Event parsers: torpedo, beam, explosion, chat, object create/destroy |
| `test_checksum.c` | 21 | 44 | StringHash, FileHash, manifest validation |
| `test_json.c` | 15 | 50 | JSON parser: objects, arrays, strings, numbers, nesting |
| `test_client_transport.c` | 12 | 93 | Client transport: connect, checksum rounds, ACK, reliable delivery |
| `test_death_repair_burst.c` | 6 | 31 | Ship death lifecycle, repair burst handling |
| `test_self_destruct.c` | 5 | 5 | Self-destruct countdown, abort, explosion damage |
| `test_combat_damage.c` | 4 | 11 | Combat damage pipeline, subsystem targeting |
| `test_player_ids.c` | 4 | 15 | Player slot ID assignment and validation |
| `test_battle.c` | 3 | 103 | Valentine's Day battle replay from real packet traces |
| `test_dynamic_battle.c` | 3 | 42 | Seeded RNG AI battle: 3-7 ships, combat/cloak/tractor/repair |
| `test_subsystem_anticheat.c` | 2 | 11 | Subsystem integrity validation, anticheat checks |
| `test_security_identity.c` | 2 | 2 | Player identity security validation |
| `test_join_flow.c` | 2 | 2 | Join flow integration: connect through gameplay |
| `test_networked_battle.c` | 1 | 9 | Real UDP through live server subprocess, packet trace logging |
| `test_restart_authorization.c` | 1 | 1 | Game restart authorization flow |
| **Total** | **290** | **1,369** | |

## Test Frameworks

### Unit tests (`test_util.h`)

Lightweight macros for unit testing:

- `TEST(name)` -- Define a test function
- `ASSERT(cond)` -- Assert a condition
- `ASSERT_EQ(a, b)` -- Assert equality (hex output on failure)
- `ASSERT_EQ_INT(a, b)` -- Assert integer equality (decimal output)
- `ASSERT_FLOAT_EQ(a, b, eps)` -- Assert float equality within epsilon
- `ASSERT_STR_EQ(a, b)` -- Assert string equality
- `RUN(name)` -- Register a test to run
- `TEST_MAIN_BEGIN()` / `TEST_MAIN_END()` -- Main function wrapper with summary

### Integration tests (`test_harness.h`)

Multi-client integration harness with:

- **Server subprocess**: Spawns `openbc-server.exe` as a child process
- **Real UDP**: Clients send and receive actual UDP packets through the server
- **Binary trace logging**: OBCTRACE format for post-mortem analysis
- **Cached packet pattern**: Handles batched transport messages (multiple messages per UDP packet)
- **Handshake helpers**: Connect, checksum exchange, settings/gameinit reception

## Integration Tests

Integration tests exercise progressively more of the stack:

1. **`test_battle.c`** -- Replays real packet captures from the Valentine's Day battle trace. Verifies that the codec produces wire-identical output to the stock server. Covers settings, gameinit, state updates, weapon fire, explosions, and object lifecycle.

2. **`test_dynamic_battle.c`** -- Spawns 3-7 AI ships with seeded RNG and runs a full battle simulation. Ships use movement, weapons (phasers + torpedoes), cloaking, tractor beams, and damage repair. Verifies combat state machine transitions and damage calculations.

3. **`test_networked_battle.c`** -- Starts a real server subprocess, connects headless clients over UDP, and runs AI combat through the live network stack. Writes binary packet traces for post-mortem analysis. The most comprehensive end-to-end test.

4. **`test_join_flow.c`** -- Full connection lifecycle through a live server: connect, checksum exchange, settings, gameinit, and gameplay entry.

5. **`test_self_destruct.c`** -- Self-destruct system: countdown initiation, abort, explosion damage application.

6. **`test_restart_authorization.c`** -- Game restart authorization flow through the server.

## Test Fixtures

- `tests/fixtures/manifest.json` -- Hash manifest for checksum validation tests
- `tests/fixtures/*.pyc` -- Python bytecode files used as checksum round test data

## Adding New Tests

Follow the existing pattern:

```c
#include "test_util.h"

TEST(my_new_test)
{
    int result = my_function(42);
    ASSERT_EQ_INT(result, 84);
}

TEST_MAIN_BEGIN()
    RUN(my_new_test);
TEST_MAIN_END()
```

The Makefile auto-discovers `tests/test_*.c` files -- no Makefile changes needed. Each test file compiles to its own `.exe`.
