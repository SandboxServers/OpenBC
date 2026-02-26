# Force-Update Timing


The serializer tracks per-field timestamps for when each field was last sent. A field is force-sent when the elapsed time since last transmission exceeds a global threshold, even if the value hasn't changed. This ensures all fields are periodically refreshed despite unreliable delivery.

When all dirty fields are sent simultaneously, the master force-update timer resets.

