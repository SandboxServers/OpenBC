# 11. Server Configuration (`server.toml`)


```toml
[server]
name = "OpenBC Deathmatch"
port = 22101                       # Default BC port (0x5655)
max_players = 8                    # 1-16
tick_rate = 30                     # Hz

[server.game]
map = "multi1"                     # Key from maps.toml
game_mode = "deathmatch"           # Key from rules.toml

[server.network]
lan_discovery = true

[server.network.master_server]
enabled = true
address = "master.333networks.com"
port = 28900
heartbeat_interval = 60

[checksum]
manifests = ["manifests/vanilla-1.1.json"]

[mods]
active = []
```

---

