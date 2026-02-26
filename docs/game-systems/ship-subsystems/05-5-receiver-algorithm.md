# 5. Receiver Algorithm


```
start_index = read_byte(stream)

node = first subsystem in list
skip forward (start_index) nodes

loop while stream has data:
    if node is null: break

    subsystem = node.data
    advance node to next

    subsystem.ReadState(stream)     // Reads exactly what WriteState wrote

    if node reached end of list:
        node = first subsystem      // Wrap to beginning
```

The receiver's `ReadState` is the exact inverse of `WriteState` — it reads the condition byte, recursively reads children, and reads any type-specific extras. Because both sides have identical subsystem lists from the same hardpoint script, the formats always match.

---

