# Focused AI recovery for invalid short-score endpoints

Historical short-score references use excerpt-local onset/release pairs. Strict JSON
schemas can bound each value, but cannot enforce the relation `beat < end_beat`.
A syntactically valid response can therefore contain a release before onset.
Previously the decoder correctly rejected it, but aborted the entire request.

The general pipeline now permits one bounded AI correction before phrase
planning. The response schema contains only the invalid reference addresses,
across initial notes, continuations, ensemble excerpts and handoff notes. The AI
sees the complete short score and chooses the corrected interval in context.
The adapter cannot add/delete notes, change pitches/owners, or edit valid
reference endpoints. A cloned candidate is fully decoded before atomic commit;
invalid or incomplete corrections retain rejection, not clipped or guessed gates.

The same request-wide deadline, cancellation token, model and reasoning effort
apply. Recovery is capped at 180 seconds and is never an unbounded retry loop.
The subsequent structural, tonal and functional checks remain in force.

Regression checks cover the reported release-before-onset case, exact target
selection, invalid intervals, unauthorized note/pitch edits, atomic failure and
preservation of valid references. This is local-only until separately deployed.

## Positive held lengths instead of absolute release arithmetic

The response for local job `abfa0976-0baf-430d-b695-d4fcc123f38d` confirmed
that even a completed AI recovery repeated short held-length values (0.33,
0.34, 0.36) as releases before onset 0.5. Repeating the endpoint instruction
did not solve this representational confusion.

New short scores use `excerpt_durations_v2`: `beat` plus a positive `duration`,
the native MIDI representation already consumed downstream. Decoding preserves
those exact numbers and still rejects overflow beyond the excerpt clock; it
does not shorten or quantize them. Endpoint-v1 and historical unversioned
references remain readable.

Focused timing recovery likewise requests `duration_beats`, not `end_beat`.
A valid onset is locked by a singleton enum and checked again during merge.
Its AI-authored gate is schema-bounded by the remaining local reference space.
For endpoint-v1 only, the adapter computes release = onset + AI gate. That
arithmetic is serialization, not a host-chosen musical duration. Invalid onsets
may be re-authored by the AI; invalid candidates still fail atomically.

Tests cover exact preservation of a 0.34-beat gate starting at 0.5, all reference
containers, legacy endpoint compatibility, onset locking, positive-gate schema
bounds, overflow rejection, and preservation of unaffected reference notes.
