# Tonal Club: an AI-written musical nucleus

Local experiment only. No Cloud deployment, production-profile promotion or
procedural generation is authorized by this experiment.

## Listening baseline

`c5b9db95-2752-4993-8f52-1c2b8f71ebf4`, **Huecos de Re**: 90 seconds
requested, 48 bars rendered, 122 BPM, 8 pitched tracks, 644 direct-AI notes.
Human evaluation: **14/45**, would not use, high confidence. Main complaints:
empty repetition, disconnected voices, awkward rhythm, insufficient sustained
Club development and a fragmented sequence.

Measured exported MIDI, not intentions:

- Sections: 10 / 14 / 8 / 16 bars.
- Bass's most common two-bar attack positions: 0, 2.5, 4.5, 6.5.
- Stabs: 2.5, 5, 6.5 within the same eight-beat cycle.
- Sequence: four attacks in the first half of most bars; the second half is
  mostly empty. Its usual gate is 0.25 beats. Repetition existed, but the
  selected recurring material did not meet the listener's Club expectation.
- Primary pad sounding fraction in the full-body second section: 0.330,
  versus 0.88 declared by the AI. Several sustained layers were cycle-bound
  independently of their harmonic-event times.

These observations do not establish that syncopation or short gates are
intrinsically bad. They identify the mismatch between this specific request,
the declared arrangement and the realized MIDI.

## Experiment

`PULSO_AI_CLUB_NUCLEUS_V2=1` is passed only by an explicitly experimental local
server. The shared profile and the default Club instructions are unchanged.

1. Clarify three timescales: recurring short groove, phrase-level arrangement,
   and patient macro development around 64 bars where duration allows it.
2. Request a concrete AI-authored `tonal_nucleus` inside the production plan:
   reference harmony, cycle, meter carrier and actual simultaneous MIDI notes
   for the relevant production lanes.
3. Supply that same reference to every writing window. The AI authors every
   actual repetition, voicing, variation and duration. There is no host-side
   tiling, pitch substitution, density filling or rhythm template.
4. Ask sustained beds to follow harmonic arrivals rather than an unrelated
   rigid attack cycle. The model chooses phrase_flexible timing for these
   layers; the renderer does not shift notes or change their timing mode.
5. Validate only the reference's technical integrity: owners, chord ID,
   finite grid positions, bounded releases, registers and the user's tonal
   policy. Old checkpoints lacking this field remain compatible.
6. Use `scripts/audit-club-nucleus.mjs` for read-only recurrence and continuity
   measurements. The report never changes MIDI or certifies musical quality.

The dedicated planning contract replaces the old text for this experiment;
it does not append contradictory "write no notes" and "write a note sketch"
instructions. A bounded 20k output budget accommodates the new reference.

## Candidate

Initial job: `f0dba464-3e5a-4ee7-b7ac-206e9698e47c`, local port 4305.
Same user prompt, tempo and seed as the baseline; duration extended to 180
seconds to give the requested larger development room. This is a new musical
candidate, **not** an isolated causal A/B: both the writing architecture and
available duration differ. API seed does not guarantee identical material.

Acceptance remains a listening decision. Technical tests confirm isolation,
schema compatibility and non-mutating validation, not artistic success.

## Internal verification and second pass

The first experiment was stopped after three accepted writing windows. Its
nucleus called a lane continuous eighths but contained attacks at 0,1,2,...7
with duration 0.5: detached quarters, not eighths. The writer faithfully
reproduced that mistaken sketch. The incomplete result and its blueprint are
preserved; it was not presented as the final listening candidate.

The next revision adds `carrier_subdivision_beats`, a model-declared metrical
promise. Validation compares the actual reference attacks, including the wrap,
with that promise. It does not insert, delete or move notes. The AI may choose
the unit and the other lanes remain free to syncopate. The prompt distinguishes
onset spacing from gate length with explicit quarter-note units.

Two further context corrections:

- Complementary bass/chord rhythms may have no shared onset. The experimental
  schema permits an empty `shared_landings`; a unison attack is not a condition
  of interlock. Nonempty declarations must still be true.
- The writer receives harmony spans in both window-local and absolute beats,
  including the chord already active before the window. It no longer has to
  mentally translate a remote harmonic event to the note response coordinates.

Second job: `494ab4c9-9257-426f-a2e0-741a61eda50c`. It reuses the first job's
accepted macro/cast via `blueprint_from_job` and requests a new production plan
and actual MIDI (`replan_phrases=true`). No rejected/provisional notes are copied.
Same 180-second request, seed, tempo and prompt. The accepted nucleus declares
0.5-beat subdivision and has sixteen attacks over its eight-beat reference;
the first exported writing windows realize that spacing.

Verification: all three C++ test targets and fourteen focused Node tests pass.
Tests cover read-only validation, self-declared subdivision consistency,
reference propagation, tonal/register boundaries, legacy compatibility,
window-local harmonic context, experimental isolation and audit correctness.

## Completed second pass and listener's rhythmic objection

Job `494ab4c9-9257-426f-a2e0-741a61eda50c` completed: 2149 direct-AI
notes, eight tracks, 88 bars at 122 BPM (approximately 173 seconds), 741 seconds
wall time, estimated API cost USD 1.3974. Completion is not musical acceptance.
The listener reports substantial rhythmic problems.

Read-only MIDI inspection confirms a concrete discontinuity at bar 9 (beat 32):
the primary pad, upper pad, sub and texture enter at 32.25; the arpeggio changes
from half-beat spacing to offbeat quarter attacks through the following window.
The production plan instead describes continuous eighths in this entire section.
The full-score reference-attack retention average hides this local failure.
The pad's approximately 99.7% sounding coverage is therefore not evidence of a
good collective attack. Neither technical quantization nor density certifies groove.

Next comparison preserves the accepted macro form, cast and concrete nucleus,
and regenerates the MIDI with explicit distinctions between attack spacing,
gate length and phrase entry. It removes the contradictory generic permission
to omit arbitrary carrier attacks from the experimental writing brief. Sustained
voices are instructed not to invent a collective sixteenth delay at technical
boundaries. These are AI writing instructions, not MIDI retiming, filling or
quantization. The separate pitch-only review experiment is NOT enabled for this
rhythmic comparison. No Cloud deployment or commit is performed.

Workflow requested by listener: after starting the next job, provide its link
and end the turn; analyze it only when the listener asks to continue.
