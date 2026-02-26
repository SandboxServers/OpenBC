# Overview


The subsystem health round-robin serializes the ship's **top-level subsystem list** in an
order determined by each ship class's hardpoint script. Each ship has a different number and
arrangement of subsystems. Both server and client build identical lists from the same
hardpoint file (verified by the checksum exchange), so they always agree on subsystem order.

For the round-robin algorithm and WriteState format details, see
[stateupdate-wire-format/](../stateupdate-wire-format/).

