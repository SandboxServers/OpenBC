# 3. Modifier Table


### 3.1 Source

`Multiplayer/Modifier.py` defines `g_kModifierTable` and `GetModifier()`.

### 3.2 Table

```python
g_kModifierTable = (
    (1.0, 1.0, 1.0),   # Class 0 attacking class 0, 1, 2
    (1.0, 1.0, 1.0),   # Class 1 attacking class 0, 1, 2
    (1.0, 3.0, 1.0),   # Class 2 attacking class 0, 1, 2
)
```

### 3.3 Lookup Function

```python
def GetModifier(attackerClass, killedClass):
    return g_kModifierTable[attackerClass][killedClass]
```

Direct table lookup with **no bounds checking**. Out-of-range indices will crash.

### 3.4 Effective Behavior

Since all vanilla ships are modifier class 1 (Section 2.4), the effective modifier for any vanilla kill is always `g_kModifierTable[1][1] = 1.0`. The 3.0 penalty at `[2][1]` was designed to penalize heavy ships (class 2) killing light ships (class 1), but no vanilla ships are assigned to either class 0 or class 2.

---

