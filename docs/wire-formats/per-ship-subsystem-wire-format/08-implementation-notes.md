# Implementation Notes


1. **Subsystem list order is ship-specific.** An implementation must build the same
   ordered list for each ship class. Mismatches cause health values to be applied to
   the wrong subsystem on the receiver.

2. **Server and client must agree.** Both sides build the list from the same hardpoint
   data (verified by the checksum exchange). This guarantees identical subsystem order.

3. **Only top-level subsystems participate in the round-robin.** Children are serialized
   recursively inside their parent's WriteState call.

4. **Shield facing HP uses a different flag.** The Shield Generator in the subsystem list
   only writes 1 condition byte. The 6 individual shield facing values use a separate
   serialization path (flag 0x40).

5. **Mod ships will have different layouts.** This catalog covers only stock ships.
   Any mod-added ship will have its own subsystem order and composition, determined
   by its hardpoint script.

6. **The subsystem name ordering varies by faction.** Even within a faction, ships
   place subsystems in different order. The order is NOT standardized — each hardpoint
   script defines its own sequence.
