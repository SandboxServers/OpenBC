# BC Test Harness (AutoHotkey)

Automates a real Bridge Commander 1.1 client to connect to an OpenBC server for manual integration testing. Complements the headless `test_battle` integration test by verifying actual game client compatibility.

## Requirements

- [AutoHotkey v2](https://www.autohotkey.com/) (v2.0+)
- Star Trek: Bridge Commander 1.1 (patched)
- OpenBC server running (`openbc-server.exe`)

## Setup

1. Start the OpenBC server:
   ```
   openbc-server.exe -v
   ```

2. Launch Bridge Commander normally.

3. Run the script:
   ```
   AutoHotkey.exe bc-test.ahk
   ```
   Or double-click `bc-test.ahk` if AHK v2 is the default handler.

4. Wait for BC to reach the main menu, then press **F5**.

## Hotkeys

| Key | Action |
|-----|--------|
| F5  | Navigate menus and join localhost server |
| F6  | Take timestamped screenshot |
| F7  | Open chat and send a test message |
| F8  | Force-close Bridge Commander |
| F9  | Toggle mouse coordinate overlay |
| F10 | Reload the script |

## Configuration

Edit the top of `bc-test.ahk` to change:

| Variable | Default | Description |
|----------|---------|-------------|
| `SERVER_IP` | `127.0.0.1` | Server IP address |
| `SERVER_PORT` | `22101` | Server port (BC default) |
| `PLAYER_NAME` | `OpenBC_Test` | Player name |
| `CLICK_DELAY` | `200` | Delay between clicks (ms) |
| `MENU_DELAY` | `1500` | Wait for menu transitions (ms) |

## Coordinate Calibration

The script assumes BC runs at **1024x768** (the default resolution). If you use a different resolution or windowed mode, press **F9** to enable the coordinate overlay, then update the `MENU_*` and `JOIN_*` coordinate constants in the script.

## What It Tests

- Real game client connects to OpenBC server via UDP
- Full handshake completes (connect, checksums, settings, game init)
- Client appears in server's player list
- Chat messages relay between server and client
- Screenshots capture visual state for debugging
