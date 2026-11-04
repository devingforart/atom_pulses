# Evidence-based musical memory and conservative review

Local Club experiment; no changes to non-Club composition or Cloud deployment.

Writing now consumes read-only accepted-score extracts: previous arc opening,
first entry, recent context, and sounding boundary notes. Exact absolute onsets,
pitches, durations and velocities are preserved. Up to 16 prioritized owners,
12 notes per extract, 384 historical notes total, 96 boundary notes; every cap
is disclosed, never interpreted as silence. Excerpts are not inferred motifs or
complete phrases. No future historical notes are exposed. Existing complete
current-window ensemble and harmonic map remain supplied separately.

The prior optional critic already sees all Club owners, carried notes, arcs and
chord palette. Corrections additionally receive the original accepted memory.
An unchanged Club proposal now ends its attempt without a forced novelty retry.
Existing outside-window identity and technical/tonal safety checks remain.
First-pass behavior is unchanged: this does not silently enable paid reviews.

`scripts/compare-musical-candidates.mjs baselineDirectory candidateDirectory`
reports multiset MIDI differences for a review of the same source, provenance,
settings and a stable human-listening rubric. It does not rank by note count or
declare musical improvement. Fresh generations and review comparisons differ.
Keep the accepted original and repeat across multiple works before promotion.

This implements symbolic ensemble evidence, NOT audio perception or training.
An autonomous audio judge is not enabled: its agreement with the listener must
be established first. No claim that these changes guarantee better music.

Validation: native pulso_pipeline_tests passes memory immutability, future-note
exclusion, carried notes, explicit truncation and owner priority, plus existing
pipeline tests. JavaScript comparison tests cover duplicate events, unchanged
reviews and polyphony versus distinct onsets. Worker builds successfully.

Trial request: fresh etude, 180 seconds, 120 BPM, seed 1176982797102497781,
Club 16-32, variety on, chromatic off, first_pass_audition=false. This deliberately
exercises the existing optional review stages as well as the new writing memory;
it is not an isolated first-pass A/B and can cost/time more. Original works remain.
