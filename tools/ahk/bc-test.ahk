; bc-test.ahk -- AutoHotkey v2 test harness for real BC 1.1 client
;
; Drives a real Bridge Commander client through its multiplayer menus
; to connect to a local (or remote) OpenBC server. Provides hotkeys
; for common test actions: join game, send chat, take screenshots, abort.
;
; Usage:
;   1. Start OpenBC server:  openbc-server.exe -v
;   2. Launch BC 1.1 normally
;   3. Run this script (double-click or: AutoHotkey.exe bc-test.ahk)
;   4. Press F5 once BC reaches the main menu
;
; Requirements:
;   - AutoHotkey v2 (https://www.autohotkey.com/)
;   - Star Trek: Bridge Commander 1.1 (window title contains "Bridge Commander")
;   - OpenBC server running on localhost:22101 (or set SERVER_IP/SERVER_PORT below)
;
; Hotkeys:
;   F5  = Navigate menus and join server
;   F6  = Take timestamped screenshot (saved to screenshots/)
;   F7  = Open chat, type "test message", send
;   F8  = Force-close BC (emergency abort)
;   F9  = Toggle coordinate display overlay
;   F10 = Reload this script

#Requires AutoHotkey v2.0
#SingleInstance Force

; --- Configuration ---
global SERVER_IP   := "127.0.0.1"
global SERVER_PORT := "22101"
global PLAYER_NAME := "OpenBC_Test"
global BC_TITLE    := "Bridge Commander"
global SCREENSHOT_DIR := A_ScriptDir "\screenshots"
global CLICK_DELAY := 200   ; ms between clicks
global MENU_DELAY  := 1500  ; ms to wait for menu transitions
global LOAD_DELAY  := 5000  ; ms to wait for level loads

; Ensure screenshot directory exists
DirCreate(SCREENSHOT_DIR)

; --- Tooltip helper ---
ShowStatus(msg, duration := 3000) {
    ToolTip(msg)
    if (duration > 0)
        SetTimer(() => ToolTip(), -duration)
}

; --- Wait for BC window ---
WaitForBC(timeout := 10000) {
    if WinWait(BC_TITLE, , timeout / 1000) {
        WinActivate(BC_TITLE)
        Sleep(500)
        return true
    }
    ShowStatus("ERROR: Bridge Commander window not found!")
    return false
}

; --- Click at screen coordinates (BC must be active + fullscreen or known size) ---
; BC 1.1 runs at 1024x768 by default. These coordinates assume that resolution.
; For windowed mode, offsets may need adjustment.

ClickAt(x, y, delay := 0) {
    if (delay > 0)
        Sleep(delay)
    Click(x, y)
    Sleep(CLICK_DELAY)
}

; --- Menu coordinates (1024x768 resolution) ---
; These are approximate center positions for BC's main menu buttons.
; BC uses a custom UI (not standard Windows controls), so we click by position.

; Main Menu
MENU_MULTIPLAYER_X := 512
MENU_MULTIPLAYER_Y := 400

; Multiplayer submenu
MENU_JOIN_X := 512
MENU_JOIN_Y := 350

; Join Game dialog -- IP address field
JOIN_IP_FIELD_X := 512
JOIN_IP_FIELD_Y := 340

; Join Game dialog -- Port field
JOIN_PORT_FIELD_X := 512
JOIN_PORT_FIELD_Y := 380

; Join Game dialog -- Player name field
JOIN_NAME_FIELD_X := 512
JOIN_NAME_FIELD_Y := 420

; Join Game dialog -- Connect button
JOIN_CONNECT_X := 512
JOIN_CONNECT_Y := 500

; ======================================================================
; F5 -- Navigate menus and join server
; ======================================================================
F5:: {
    ShowStatus("Joining server at " SERVER_IP ":" SERVER_PORT "...")

    if !WaitForBC()
        return

    ; Step 1: Click "Multiplayer" on main menu
    ShowStatus("Step 1/5: Clicking Multiplayer...")
    ClickAt(MENU_MULTIPLAYER_X, MENU_MULTIPLAYER_Y)
    Sleep(MENU_DELAY)

    ; Step 2: Click "Join Game"
    ShowStatus("Step 2/5: Clicking Join Game...")
    ClickAt(MENU_JOIN_X, MENU_JOIN_Y)
    Sleep(MENU_DELAY)

    ; Step 3: Clear and type IP address
    ShowStatus("Step 3/5: Entering IP " SERVER_IP "...")
    ClickAt(JOIN_IP_FIELD_X, JOIN_IP_FIELD_Y)
    Sleep(200)
    Send("^a")  ; Select all
    Sleep(100)
    Send(SERVER_IP)
    Sleep(200)

    ; Step 4: Tab to port field (if separate) and enter port
    ; BC's join dialog may have IP:port in one field or separate fields.
    ; Try tabbing to port field:
    Send("{Tab}")
    Sleep(200)
    Send("^a")
    Send(SERVER_PORT)
    Sleep(200)

    ; Step 5: Click Connect
    ShowStatus("Step 5/5: Connecting...")
    ClickAt(JOIN_CONNECT_X, JOIN_CONNECT_Y)
    Sleep(LOAD_DELAY)

    ShowStatus("Join sequence complete. Check server logs.", 5000)
}

; ======================================================================
; F6 -- Take timestamped screenshot
; ======================================================================
F6:: {
    if !WinExist(BC_TITLE) {
        ShowStatus("BC not running!")
        return
    }
    WinActivate(BC_TITLE)
    Sleep(200)

    ; BC has a built-in screenshot key (PrintScreen), but we also save via AHK
    timestamp := FormatTime(, "yyyyMMdd_HHmmss")
    filename := SCREENSHOT_DIR "\" timestamp ".png"

    ; Use BC's own screenshot (F12 in some configs, or PrintScreen)
    Send("{PrintScreen}")
    Sleep(500)

    ShowStatus("Screenshot: " timestamp ".png", 3000)
}

; ======================================================================
; F7 -- Send chat message
; ======================================================================
F7:: {
    if !WaitForBC(3000)
        return

    ShowStatus("Sending chat message...")

    ; BC chat: press Enter to open chat, type message, press Enter to send
    Send("{Enter}")
    Sleep(500)
    Send("OpenBC test message - " FormatTime(, "HH:mm:ss"))
    Sleep(200)
    Send("{Enter}")

    ShowStatus("Chat sent.", 2000)
}

; ======================================================================
; F8 -- Force-close BC (emergency abort)
; ======================================================================
F8:: {
    if WinExist(BC_TITLE) {
        ShowStatus("Force-closing Bridge Commander...")
        WinKill(BC_TITLE)
        Sleep(1000)
        ; If still alive, use taskkill
        if WinExist(BC_TITLE) {
            Run('taskkill /F /IM "Star Trek Bridge Commander.exe"', , "Hide")
        }
        ShowStatus("BC closed.", 3000)
    } else {
        ShowStatus("BC not running.", 2000)
    }
}

; ======================================================================
; F9 -- Toggle coordinate display overlay
; ======================================================================
global CoordOverlayActive := false

F9:: {
    global CoordOverlayActive
    CoordOverlayActive := !CoordOverlayActive

    if (CoordOverlayActive) {
        ShowStatus("Coordinate overlay ON (move mouse, F9 to stop)")
        SetTimer(ShowCoords, 100)
    } else {
        SetTimer(ShowCoords, 0)
        ToolTip()
    }
}

ShowCoords() {
    MouseGetPos(&mx, &my)
    ToolTip("X: " mx "  Y: " my)
}

; ======================================================================
; F10 -- Reload script
; ======================================================================
F10:: {
    Reload()
}

; ======================================================================
; Startup
; ======================================================================
ShowStatus("BC Test Harness loaded.`nF5=Join  F6=Screenshot  F7=Chat  F8=Abort  F9=Coords  F10=Reload", 5000)
