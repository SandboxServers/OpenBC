# Modding Conventions - Developer Intent

## Custom/ Directory
- Located at scripts/Custom/
- Imported by Local.py during startup
- INTENTIONALLY exempt from multiplayer checksums
- Purpose: Client-side mods that don't affect gameplay simulation
- Examples: Custom bridges, key bindings, UI mods, server automation
- DedicatedServer.py lives here because it's server-side only

## Event System Design for Moddability
- Handlers registered by STRING NAME: "Module.FunctionName"
- Late binding: function looked up at dispatch time, not registration
- Module reload: change script, reload module, new handler takes effect
- Handler chains: CallNextHandler enables multiple listeners per event
- Two registration types:
  - AddBroadcastPythonFuncHandler: fires for any event of that type
  - AddPythonFuncHandlerForInstance: fires only for events to specific object

## Python Script Architecture
- C++ layer: network plumbing, player slots, connection lifecycle
- Python layer: gameplay rules, scoring, chat, ship selection
- Separation is deliberate: C++ handles transport, Python handles logic
- Mission scripts (Mission1-5) define game mode rules
- MissionShared.py: common multiplayer logic (scoring, time limits, end game)
- MissionMenusShared.py: lobby UI and settings management

## Checksum Convention for Mods
- Gameplay-affecting mods go in checksummed directories (scripts/ships/, etc.)
- Both players need identical mod packs
- Cosmetic/client-side mods go in Custom/ (no matching required)
- Community developed conventions around this organically

## GameSpy Server Browser
- Uses QR protocol on same UDP socket as game traffic
- '\'-prefixed text queries demuxed from binary game packets
- Basic info: hostname, numplayers, maxplayers, gametype
- Rules: system, timelimit, fraglimit, mapname
- LAN discovery uses broadcast, same protocol
- GameSpy master server long dead; LAN still works
