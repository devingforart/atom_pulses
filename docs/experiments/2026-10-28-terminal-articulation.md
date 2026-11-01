# Bounded terminal articulation

Job 83de460f-4492-47b3-a0ea-eb4976866de6 reached window 11/11 but
seven attempts failed exact farewell-event checks. Several returned the correct
final pitch and onset with duration 3 or 3.25 instead of 3.5 beats. Other attempts
shifted attacks by half a beat, which remains invalid: this is not release articulation.

The read-only final-event verifier now permits release differences up to 20%
of the planned duration, capped at half a beat. This is an explicit engineering
tolerance, not an aesthetic certification. Exact pitch/onset, terminal bounds,
same-pitch overlap checks and required arrival remain enforced. Short notes do
not gain a blanket half-beat tolerance. No MIDI is extended, moved or replaced.

Test a fresh work with the same prompt and settings, without reusing the failed
score. Preserve the checkpoint for diagnosis. Cloud is unchanged.
