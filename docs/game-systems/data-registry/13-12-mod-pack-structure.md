# 12. Mod Pack Structure


### 12.1 Directory Layout

```
mods/
  my-mod/
    mod.toml               # Mod metadata (required)
    ships.toml              # Additional/modified ship definitions (optional)
    maps.toml               # Additional maps (optional)
    rules.toml              # Custom game rules (optional)
    species.toml            # Additional species definitions (optional)
    modifiers.toml          # Replacement modifier table (optional)
```

### 12.2 Merge Semantics

| Data Type | Merge Behavior |
|-----------|---------------|
| `ships.toml` | Additive with override. New keys added, existing keys fully replaced. |
| `maps.toml` | Additive with override. |
| `rules.toml` | Additive with override. |
| `species.toml` | Additive. Duplicate species IDs are an error. |
| `modifiers.toml` | Full replacement. Last-loaded table wins entirely. |

### 12.3 Client-Side Behavior

- Mods affecting only server-side data (stats, rules, modifiers) require **no** client changes.
- Mods adding new ships require matching client scripts/models. The mod's hash manifest validates this.
- Mods in `scripts/Custom/` bypass checksumming entirely.

---

