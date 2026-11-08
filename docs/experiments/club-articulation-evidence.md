# Melodic articulation evidence (local experiment)

Positive source anchor: 92b8f02, job 04eb115b-3366-4fad-8797-7c9498851081.
The listener accepted improvement. Preserve that MIDI and engine source.

This candidate adds read-only observations to the existing Club AI critic:
overlapping same-pitch reattacks within half a beat, and short register leaps
of at least an octave within two beats. These are bounded search heuristics,
not forbidden musical events or automatic acceptance/rejection conditions.
Only owners named in melodic arcs are included; simultaneous chord attacks
are not classified as consecutive melodic leaps. At most 32 examples are
sent, with omitted count disclosed. Full existing local note context remains.

The AI decides whether a candidate is intentional in its phrase and polyphony.
Existing limited revisions and safety checks remain in place. No note removal,
pitch snapping, new repair loop, minimum note count or extra API stage is added.
Writer guidance asks for articulated recurrence and motivated register changes,
without flattening angular music or eliminating expressive repetition.

Native tests cover observations, immutability, background exclusion and distinct
same-pitch articulation. Fresh trial: same prompt, seed and three-minute request.
This is a musical hypothesis, not a demonstrated improvement until listening.
