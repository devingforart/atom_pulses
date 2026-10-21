# Related materials and evolving musical momentum (local experiment)

## Provisional reference and hypothesis

Reference: `758af1f0-ea2b-4bcc-933b-db01102b3ee6`, the whole-argument version of
`The Restored Breath`. Listener feedback: improved melodic dialogue/connection,
but a monotonous perceived pulse and too little motivic variety or consequential
surprise. Preserve this as a partial success, not a production promotion.

The reference plan declared `thematic_materials: []`; every phrase named
`material_development.material_id: primary`. This supports investigating its
material palette, but does NOT prove all its sounding motifs are identical.
Perceived musical dynamics is not equivalent to the range of MIDI velocities.

## Generic implementation

`AiMaterialDialogue` is an opt-in module, requiring whole-argument composition.
The existing complete subject and optional secondary reference notes are retained.
`material_design` describes each chosen material's recognizable identity, distinct
contribution, interaction, motion partners and developmental states. States explain
what is retained, what changes in continuation, how movement is distributed, and
the consequence of that change. These are AI-authored intentions, not proof that
the actual MIDI enacts them.

There is no minimum number of themes/states, no timer for changes, no required
asymmetry, no density target and no algorithm performing inversion, duplication,
transposition, rhythmic variation, note deletion, forced silence or humanization.
A monothematic work and intentionally stable ostinati remain valid. Existing
array maxima are wire-capacity bounds, not desired composition quantities.

The AI may select a harmonic/motion carrier in its complete argument if its
actual notes are essential to the development. It still composes the whole
selected reduced score in one response, followed by one bounded scoped AI review
and accompaniment against immutable selected notes. New descriptions reach all
three stages. In particular, the accompaniment now sees `material_design` AND
each relevant phrase's existing `material_development`; previously its compact
window brief omitted that latter field. Supporting roles can participate in
development without every part becoming a soloist.

Technical validation checks bounded descriptive fields, declared material IDs,
real motion-partner cast IDs and thematic speakers belonging to the selected
argument. It does not grade or reject music for being repetitive, monothematic,
sparse, symmetrical or quiet. Invalid references are not silently mapped to a
different voice or theme. Validation never changes reference notes.

The schema uses [OpenAI structured outputs](https://developers.openai.com/api/docs/guides/structured-outputs).
This provides an explicit data shape, not a guarantee of musical accuracy/value.

## Isolation and trials

Launcher: `node web/local-studio/start-material-dialogue.mjs`, default port 4207.
Flag `PULSO_AI_MATERIAL_DIALOGUE_EXPERIMENT` is stripped by ordinary worker
environments; accepted whole-argument behavior is unchanged with it disabled.
No production default, Cloud deployment, VST installation or commit is included.

- Main A/B candidate `988ef01f-11b0-40bc-8b97-852c12a98854`: requested 180 seconds,
  120 BPM, adaptive, AI sovereign, seed `202610200101`, prompt
  `Musica como la de mozart.` Only the reference's macro/cast blueprint is reused;
  phrase planning is new and no performed MIDI is imported. This holds harmonic
  form context while changing the conditioning and orchestration context.
- Fresh work `349e82c8-083d-41f0-987c-6c524a1cd946`: requested 60 seconds, 120 BPM,
  adaptive, AI sovereign, seed `202610200201`, same prompt, all planning from
  scratch. A short generalization check, not evidence about every musical style.

Both use the currently configured Terra/medium model. Human listening must compare
continuity, actual material development, changing momentum and earned closure.
More references, more notes or a greater number of duration values are not scores
of musical quality. Neither trial is promoted merely for completing technically.

## Main comparison: observed results

`988ef01f-11b0-40bc-8b97-852c12a98854` completed at 22:31:38 ART on
2026-10-20. The inherited macro form remains 88 bars / 176 seconds in C major.
The AI chose one theme with three development states, not secondary references.
The whole reduced score completed in one response. Its AI rehearsal kept that
first draft; the subsequent accompaniment preserved every selected note's pitch,
onset, duration and velocity exactly. The 896 final notes are all reported as
`direct_ai`, with zero reused/transformed/other notes. The selected reduction has
614 notes; no unterminated notes or within-track same-pitch overlaps were found.

Actual listening landmarks (same form as the reference):

- 00:32 / bar 17: principal violin continuation.
- 00:56 / bar 29: oboe takes the foreground; violin is absent.
- 01:44 / bar 53: violin returns. Its local duration mix shifts to 39 quarter,
  eight half and ten eighth notes, versus 23 quarter, five half and 48 eighth
  notes in the opening. This establishes a changed surface, not narrative merit.
- 02:16 / bar 69: culmination; 02:40 / bar 81: closing phrase.

Important counterevidence: 165 of the bassoon's 166 notes last two beats. The
reference bassoon had 103 notes with two-, three-, four- and eight-beat durations.
The candidate has more foreground activity, but this may sound busier rather than
more naturally dynamic. Do NOT declare the monotony problem solved, or replace
the reference without listening. One-theme selection is not itself a failure;
nor is the model's positive rehearsal diagnosis independent listening evidence.

The original full MIDI remains byte-identical to its pre-trial SHA-256:
`174e12bec0413107ab29ce42b28137e03c500328365fa8f87eb24b30a809ca12`.

Audition candidate: `http://localhost:4207/?job=988ef01f-11b0-40bc-8b97-852c12a98854&argument=1`.
Reference: `http://localhost:4206/?job=758af1f0-ea2b-4bcc-933b-db01102b3ee6&argument=1`.

## Fresh one-minute work

`349e82c8-083d-41f0-987c-6c524a1cd946`, `The D Question, Answered in G`,
completed at 22:33:23 ART. It is 30 bars / 60 seconds in G major. The planner
needed one technical retry because a named phrase speaker was absent from its
selected argument-owner list. The retry was an AI-authored correction; no cast
identity was silently reassigned. The complete reduced-score writing then took
one response, with four selected owners. The AI again chose one primary thought,
this time with four planned states; no extra theme was forced.

The scoped rehearsal added 22 second-violin notes to the closing return without
removing or changing its 33 earlier notes. All first-violin, viola and cello notes
were unchanged. The realized eight-track arrangement has 498 direct-AI notes,
zero transformed/reused/other notes, no unterminated notes or within-track
same-pitch overlaps. All 235 selected foreground notes survived orchestration
exactly. Supporting bassoon/harp durations are more varied here than in the main
trial; this is evidence of a different result, not proof of human-level phrasing.

Listening landmarks: opening at 00:00, lower reply at 00:16, transformation at
00:32, and restored answering voice / closing sentence at 00:48. Audition via
the shared main lab: `http://localhost:4207/?job=349e82c8-083d-41f0-987c-6c524a1cd946&argument=1`.

## Verification and status

The isolated worker and phrase-development C++ target built successfully.
The C++ representation/authorship/isolation checks and 19 targeted Node checks
passed. HTTP GET of both full MIDI and selected reduction returned 200 from the
main lab. No browser/audio listening capability was available to the agent;
therefore perceptual superiority remains unestablished. Human audition is still
required before selecting a new musical anchor or changing production defaults.
No Cloud deployment, VST rebuild/installation, Git commit or push was performed.
