# Local joint ensemble scenes

Opt-in: `PULSO_AI_CLUB_ENSEMBLE_SCENES=1`. No Cloud default change.

The AI plans each formal section as a scene: inherited material, foreground,
owned secondary ideas, their relationship and speaking spans, transition and
musical consequence. It receives the previous/current/next scene while writing.
For casts of at most 12 owners, all voices are written together in each existing
technical window rather than completing the foreground in a separate pass.
Larger casts retain bounded groups sharing the scene context. Technical windows
remain bounded; they are not imposed musical phrase boundaries.

Scene metadata is persisted in the phrase plan and survives checkpoint resume.
It takes precedence over an older foreground-first marker. Explicit AI-planned
breaths remain valid. No notes, rhythms, fills or pitches are synthesized by
this extension; validation checks metadata integrity, not aesthetic quality.

Compare a fresh 180-second Club etude, seed 1176982797102497781, 120 BPM,
16–32 layer pace, no chromaticism, with the preceding foreground-first trial.
The positive anchor remains 92b8f02. Musical improvement requires listening;
passing tests or adding scenes is not evidence of a better composition.

## Phrase realization follow-up

Trial 34b01801 completed, but foreground material shrank while accompaniment grew.
Its worker and relevant source snapshots are preserved under
`build/anchors/ensemble-scenes-34b01801` (local, not a deployed version).

New AI scene voices explicitly describe statement, continuation and arrival.
Stable pads and pulses are allowed to remain stable; no note-count quota is added.
The writer receives actual opening and latest notes for each named scene voice,
from the preceding and current scene, bounded to 24 notes per voice / 12 voices
with explicit omission counts. Future attacks are excluded. This complements
the existing general score memory rather than replacing it with prose.
The existing focused AI critic receives the same scene commitments to compare
with its rendered-note evidence and can request targeted owner/window revisions.
Existing safety checks and revision budget remain unchanged. Historical plans
without these optional semantic fields remain readable. No procedural note edits.
