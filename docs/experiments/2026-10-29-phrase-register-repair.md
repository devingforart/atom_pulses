# Focused phrase register repair

Failure 004076b3-b2be-4ce0-955e-f7235a7fe152: secondary_sentence's cast
register was 66..82. Both complete planner responses used pitches 83 and 84.
They are diatonic, but violate the same register used by the downstream writer.

The validator now reports PHRASE_REGISTER_CONFLICT with owner, arc, local beat,
pitch and permitted range. Nested ensemble parts retain their actual arc start.
The bounded retry receives the rejected draft and instructions to preserve form,
harmony, cast and unrelated voices while recomposing conflicting passages.
It still returns the complete schema: this is not a partial JSON patch protocol.

No automatic transposition, deletion or widening of owner ranges. Dynamically
renegotiating cast ranges across the entire pipeline is NOT implemented here;
loosening only this validator would leave later stages inconsistent. The AI
repairs its phrase against the existing contract. Tests check actionable
diagnostics and immutable notes. No extra attempts or Cloud deployment.

Launch a new work from scratch with the same three-minute Club prompt and
settings. This does not reuse the failed blueprint or MIDI.
