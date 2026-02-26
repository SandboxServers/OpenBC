# 4. Round-Robin Serialization Algorithm


The server sends subsystem health data in a cycling window, not all at once:

```
state:
    cursor       // Current position in the subsystem list (persists across ticks)
    index        // Integer index of cursor position

algorithm:
    if cursor is uninitialized:
        cursor = first subsystem in list
        index = 0

    initial_cursor = cursor
    write_byte(index)           // start_index tells receiver where we begin

    loop:
        subsystem = cursor.data
        advance cursor to next
        increment index

        subsystem.WriteState(stream, isOwnShip)

        if cursor reached end of list:
            cursor = first subsystem in list
            index = 0

        if cursor == initial_cursor:
            break                   // Full cycle complete

        if total_bytes_written >= 10:
            break                   // Budget exhausted (10 bytes including start_index)

    // cursor and index persist for next tick
```

The budget check uses **total stream bytes written** (including the `start_index` byte), not just subsystem data bytes. This means 9 bytes are available for actual subsystem data.

Over multiple ticks, every subsystem gets its health synchronized. For a ship with 11 top-level systems, a full cycle takes ~4-6 ticks depending on how many children each system has and whether this is a remote or own ship.

---

