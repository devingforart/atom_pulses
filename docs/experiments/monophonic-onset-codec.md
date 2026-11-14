# One authored event per monophonic onset

The Club trial 1147b93a-683c-453f-b50f-e58692bf9cf6 failed in window 4/4.
The final raw AI response contained two D5 / pitch 74 attacks at beat 2 with
different velocities in pulse_high_static. This was a duplicated same-pitch
attack, not a valid chord and not a MIDI export artifact. Repeated feedback
did not prevent the model from returning another duplicate in the flat array.

Bounded cyclic monophonic static layers now use an onset-keyed representation:
events maps each already permitted window-local beat to one note object or null.
The beat keys come from the existing AI-authored timing grammar and activity
bounds. No new beat, note, accent, pitch or repeat is chosen by this codec.
The AI chooses whether to attack or rest and supplies pitch, duration, velocity.
One slot cannot contain two note objects. Canonical beat-key spelling avoids
different keys denoting the same numeric onset. Decoding is serialization only.

Polyphonic chord stabs retain their chord-capable array/repeat schema. Flexible
melodic voices retain their existing representation. Historical arrays remain
readable, with the duplicate-attack check still active; none is deduplicated.
Existing window/scale/pitch/phase/activity checks still run after decoding.
Empty or null events are not expanded into host-authored notes. The retry uses
the accepted checkpoint: no need to regenerate the preceding accepted windows.

Tests cover actual per-window slot schema, exact timing/pitch/gate/velocity
decoding, null rests, malformed values, alternate key spellings, phase mismatch,
polyphonic exclusion, legacy duplicate rejection and exported MIDI round-trip.
