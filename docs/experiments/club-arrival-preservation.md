# AI arrival preservation — local experiment

## Observed regression

Job `7863f8f1-a56e-4c6b-bb74-8734b2c14df7`, partial-12: protagonist ends
at beat 340, MIDI pitch 74 (D5), duration 3.75 beats. Partial-13 and final:
the terminal rewrite ends at beat 346.5, pitch 76 (E5), duration 1 beat.
The scene plan promised final D5. E belongs to the scale; scale compliance
and the tonic chord bed did not establish completion of the melodic promise.

The terminal rewrite's independent A/B comparison previously ran only in a
special completed-checkpoint experiment. Fresh scene generations could accept
a candidate based on structural metrics without that comparison.

## Change

For scene-based Club plans, technically safe terminal candidates now also need
an AI comparison against the unchanged baseline. Both note excerpts include
carried-in sustained notes, the scene/arc contract, final harmony/pitch classes
and the actual protagonist entry (which may occur later than the first 8 bars).
The writer receives the same arrival context before proposing a change.

The verdict explicitly reports whether the original and candidate realize the
planned goal, whether an existing successful arrival survives, and MIDI-grounded
evidence/reason. Unknown, malformed, unavailable or nonpreferred verdicts retain
the baseline. At most the existing two rewrite attempts are used. No tonic rule,
pitch substitution, duration quota, additional notes or quantization is added.
AI judgment can still be wrong: human listening remains required.

Scope: the fresh scene experiment's terminal rewrite stage. Existing technical
safety checks remain; other editorial paths and Cloud defaults are unchanged.
The preceding worker/source snapshot is in `build/anchors/scene-phrase-7863f8f1`.
