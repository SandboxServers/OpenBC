# Conceptual Architecture


```
                    ┌──────────────┐
                    │   Reactor    │ ← Has its own HP. Output scaled by health.
                    │  (generates) │
                    └──────┬───────┘
                           │ PowerOutput * conditionPct  (per second)
                           ▼
              ┌────────────────────────┐
              │    Main Battery        │ ← Fills first. Overflow goes to backup.
              │  (up to MainBatLimit)  │
              └────────────┬───────────┘
                           │                    ┌────────────────────────┐
                           │    overflow ──────►│    Backup Battery      │
                           │                    │ (up to BackupBatLimit) │
                           │                    └────────────┬───────────┘
                           ▼                                 ▼
              ┌─────────────────┐               ┌─────────────────┐
              │  Main Conduit   │               │ Backup Conduit  │
              │ (health-scaled) │               │  (NOT scaled)   │
              └────────┬────────┘               └────────┬────────┘
                       │                                  │
                       └──────────┬───────────────────────┘
                                  ▼
                    ┌──────────────────────────┐
                    │   Consumer Subsystems    │
                    │  (shields, engines, ...)  │
                    └──────────────────────────┘
```

---

