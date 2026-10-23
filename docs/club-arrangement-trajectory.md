# Club: audible entry, development and release

## Baseline

The accepted `Hipnosis de Re` checkpoint is
`5766a697-0d3b-4814-848f-ed20f11d14dd`: 88 bars, 120 BPM, D minor,
2584 AI-authored notes. Six voices attack at beat zero. Seven voices remain
in the last bar; the final release coincides with beat 352. The melodic tonic
arrival is real, but the accompaniment continues almost to the file boundary.
The production text promises withdrawals which the actual MIDI does not realize.
Passing the existing ending review does not establish a convincing audible ending.

## Generator change

Club production states now ask the AI for `entry_beat` and `release_beat`,
measured from the section start. The AI decides these positions, musical material,
handoffs, notes and rests. An activity span is permission, not a minimum density
or an instruction to play continuously. Absent states use zero/zero.

The direct-note output schema intersects the activity span with the technical
window and the AI's cyclic attack grammar. The parser independently checks those
bounds. An invalid note is returned for AI revision; it is never cut, shifted,
lengthened, transposed or filled locally. Planned empty windows and delayed core
entries are legal. Missing-layer recovery respects the available activity span.

Instructions distinguish a complete song from a repeatable production section.
Full songs should reveal their identity, reserve a recognizable return and finish
their gestures in a coordinated order. No fixed intro length, voice quota,
mandatory fade or prescribed ending pattern is introduced. Rich middle sections
remain desirable. Existing plans without activity fields retain their old spans.

`scripts/audit-club-trajectory.mjs` reports actual entrances, releases, bar activity
and contract violations without modifying MIDI or claiming an aesthetic grade.
Its tests cover section-local coordinates, release violations and legacy plans.
Pipeline tests cover validation, early attacks, overlong releases, legal exact
boundaries and deliberate core silence.

## Controlled comparison

Local Auto job `6fb7b1b9-08ad-46d3-ae6b-b0dae3703fe1` reuses the baseline macro
blueprint, cast, prompt, duration and seed, with `replan_phrases=true`. The AI writes
a new production plan and new notes: this is not a claim of pitch-identical middle
passages or an edit of the user's original MIDI. It runs on the experimental
server at port 4215; the original remains intact in the shared local library.
This controlled local comparison remains available as a reference for Cloud
generations using the same shared composer profile.

The initial run resumed as `31fe3a01-a8c8-4804-b61b-754795aff102` and saved
9/11 windows (2223 notes). Multiple support requests with heterogeneous release
schemas remained queued until their transport deadlines. This was not a musical
validation rejection. Resume `e811c171-5588-4694-a611-3b8bdddadbd1` reproduced the
same transport problem and was stopped.

Support requests now batch by their window-clipped release deadline, retaining
the jointly written groove core and supplying accepted MIDI to each later batch.
This changes technical batching, not the AI-authored activity plan. Flexible
owners with unrestricted window spans reuse the common note schema. A fully
withdrawn group is skipped before requesting/rendering music. Pipeline tests
cover schema intersection and batching compatibility as well as parser limits.

Resume `6bd4dc55-d612-44fc-bb12-4bbf3783c890` uses this implementation and the same
2223-note checkpoint. It completed successfully with 2687 directly AI-authored
notes. The first 2223 notes compare exactly with the accepted prefix.

## Audible candidate and limits

Candidate: http://localhost:4215/?job=6bd4dc55-d612-44fc-bb12-4bbf3783c890

Original: http://localhost:4215/?job=5766a697-0d3b-4814-848f-ed20f11d14dd

The opening now starts with three voices rather than six; arpeggio enters at
beat 8.5 and melody at beat 16. Upper pad, stabs and texture enter at beats
80, 89 and 96. Melody sounds in 69/88 bars instead of all 88.
The last bar has five attacked notes instead of sixteen. Bass and melody
sustain D for four beats; arpeggio finishes its A-F-D descent at beat 347.5,
chord pulse releases at 349.5, upper pad at 351, and the final D releases at
352. There is still no post-release tail encoded as MIDI duration.

Activity-plan violations and cyclic-phase mismatches are zero. Existing final
review reports ready, eight developed voices, harmonic continuity 0.977,
climax contrast 1.432 and resolution 0.649. These are not listening grades.
An exposed same-pitch overlap remains in the melody: A at beats 290.25 and
290.5 (bar 73). Held diatonic tensions remain, including compound minor ninths;
their count did not improve uniformly. The candidate is an entrance/ending
comparison, not a claim of a flawless composition. No notes were manually
retimed or removed to hide these observations. Human listening is pending.

The musical comparison must inspect phrase development and harmonic preparation,
not reward fewer voices at the edges as proof of quality. A new rendering needs
human listening before it can replace the approved anchor.
