# Local phrase-articulation experiment: The Fourth That Returns

This is an isolated MIDI editing/listening experiment, not an activated composer
policy, approved anchor or Cloud release. No VST compilation or deployment.

**Research handoff:** C/D/E are diagnostic audition artifacts, not generalized
composer improvements. Further experiments must follow
[the reusable-generator protocol](COMPOSITION_EXPERIMENT_PROTOCOL.md): change the
experimental generation workflow, compare on the source work, and verify transfer
to new works before calling the result general progress.

## Listening evidence and scope

Original: `f3f73a2f-8b8a-43ba-9a96-7ddac7a4e259`.
The listener identified bars 2–4, especially bar 3, of First violin protagonist
and Second violin motif answerer as awkward through the complete MIDI on piano.
The listener explicitly likes the rhythmic complexity at bar 53; that passage
must not be treated as a defect or revised by this experiment.

Timing-only B: `42855f51-4b28-4d46-8417-73e8f79ed335`, rejected by the listener.
Keeping every pitch and note event did not establish better articulation. The AI
shortened the D4 at absolute beat 7.75 to 0.25 beat but retained the D4 at 8.5.
Its prose explanation was not evidence of musical improvement.

## Phrase-level candidate C

Draft: `489f36d1-75de-4c79-bc48-f0ddf38de8f8`.
The AI may rewrite the two related voices together at absolute beats [4,16)
(bars 2–4, quarter-note units). All other notes and musical events are immutable.
The opening and the continuation from bar 5 remain original. No procedural note
generation, pitch snapping, rhythmic straightening, note merging or thinning is
performed. A representation-only serializer writes the notes explicitly returned
by the model. There is no automatic musical-quality rejection threshold.

The actual candidate replaces 13 source notes with 12 AI-authored notes:

- The protagonist's D4 attacks at 7.75 and 8.5 become one D4 at 7.5 lasting
  2.75 beats (1.375 seconds at the existing 120 BPM).
- The answering violin starts at 10.5 after that arrival releases at 10.25.
  It develops G4–F4–E4, then F4–E4–D4, and hands over at 14.5.
- The protagonist's G4 at 14.5 sustains to the original A4 at beat 16.
- The original near reattack of E4 at beats 6/6.5, across the two voices, is absent.

These are symbolic reasons to present C for listening, NOT a declaration that
it sounds better. Eliminating near reattacks is not a general composition rule:
repeated notes, overlapping voices and syncopation can be intentional and good.
C has not been accepted by the listener. Original and rejected B are preserved.

Artifacts live in ignored `build/articulation-experiments/<draft>/`: exact API
request (without credentials), response/usage, revision, audit and candidate MIDI.
Only selected candidates are published to the local Auto history. Full original
and candidate scores are compared with unchanged notes outside the edit scope;
each individual track is checked against the matching full-score track.

## Tools

`tools/audition-ai-phrase-edit.mjs generate <source-job>` requests a draft without
publishing it; `materialize <draft>` rechecks existing response notes without an
API call; `publish <draft>` publishes that checked draft without regenerating it.
The script uses the existing Terra/medium model. Inspect actual notes before
publishing; a model's explanation or technical success is not a listening result.

`tools/lib/midi-phrase-replacement.mjs` preserves non-note events, headers, tempo,
and untouched track bytes. It rejects unrepresentable or out-of-scope notes rather
than repairing them. Three focused serialization tests cover note-count changes,
scope/owner rejection, immutable notes, and same-pitch note-off/on integrity.

The API response contract uses [OpenAI structured outputs](https://developers.openai.com/api/docs/guides/structured-outputs).

## Following C: strongest passage and narrative ending

The listener reports C as improving, not accepted as finished: some melodic attacks
remain awkward at the strongest passage and the ending reduces intensity without
clearly communicating a resolution. This feedback does NOT authorize flattening
the whole piece or revising bar 53, whose complexity the listener explicitly likes.

The study tool accepts an optional JSON commission specifying interval, owners,
title and listening instructions. The executable applies no genre-specific musical
rules: commissions under `experiments/articulation-studies/` belong only to this
particular controlled comparison. Request snapshots preserve the exact instruction
version used, even if the study commission is subsequently refined.

Private climax drafts (all source C, none presented as improvements):

- `0ca8b1b9-dec0-4796-b7d2-b62f136e5d67`: not selected. Repeated subject cells,
  52 lead notes instead of 30 and a B6 peak instead of G6 made a larger rewrite
  without addressing the stated problem of awkwardness.
- `ae197d8f-c9e6-4759-9ebd-8420141c9c50`: not selected. The answering violin grew
  from 10 to 28 notes and often shadowed the clarinet at nearby same-pitch attacks.
  A favorable explanation did not override the actual MIDI evidence.
- `536c8dff-e128-4cae-a9d2-43b2fec1d4c9`: candidate for comparison. The G6 at
  beat 240 lasts two beats instead of one, several links are longer, the answering
  violin remains the original sparse ten-note part, and the clarinet's competing
  descent is less busy. The viola is original. The actual C#5 at 268 still prepares
  the unedited answer at 272. Original first ascent and all music before 224 remain
  intact. None of these numerical observations establishes listening quality.

Private closing drafts:

- `680e2752-f4c5-4eec-923c-e0a8a2e68dca`: melody-only, not selected. It introduces
  thematic liquidation, but then holds D4 for 14 beats while the unchanged tonic
  accompaniment continues reattacking. A final sustained note alone is insufficient
  evidence of a communicated narrative conclusion.
- `78e7fa33-bb65-4273-940b-74bf3a56ce27`: whole pitched ensemble, not selected.
  The actual final D4 begins at 332.75, well before the proposed A7-to-Dm change
  at 336, and sustains for 15.25 beats. Clarinet and second violin also duplicate
  a line. The description claimed a coordinated conclusion more convincingly than
  the notes supported. It is preserved as evidence, not silently corrected.

An ensemble closing study can own a complete section's pitched parts and explicitly
return a revised harmonic timeline using existing chord IDs. Its actual MIDI notes
are authoritative; changing harmonic labels does not alter notes. Publication updates
that section's harmonic events and records that original performance cells are
historical, not the edited MIDI's note authority. It never rerenders or procedurally
retunes the score. No tempo or percussion changes occur.

Two selected, disjoint drafts against the same immutable source can be composed by
`publish <primary-draft> <companion-draft>`. Overlapping owner/time scopes are rejected
rather than implicitly overwriting one another. The single-region drafts are retained;
publication records both source drafts and checks individual/full-score agreement.

### Selected combined candidate D (pending human listening)

Published local Auto job: `30f2831b-4dca-4c30-88f0-e91835e9ee87`.
Its two non-overlapping regions are the third climax draft
`536c8dff-e128-4cae-a9d2-43b2fec1d4c9` and the following ensemble ending:

- `150bd544-1a5d-4428-a2f4-b8c32e253e0f` gives an actual Gm → Bbmaj7 → A7 → Dm
  approach at beats 320, 328, 332, 344. The final tonic is no longer anticipated
  several beats early; the final ensemble releases at 349.5. A connected lead
  sentence precedes that arrival, rather than four isolated notes.
- Focused AI revision `30f2831b-4dca-4c30-88f0-e91835e9ee87` changes ONLY that
  draft's second-violin and clarinet parts. The second violin supplies a single
  related answer, then supports the cadence; the AI leaves the clarinet silent
  for this coda. The main line, harmonic timeline and all other parts are copied
  exactly from the previous AI draft, not regenerated or modified algorithmically.

The final sounding pitch classes at beat 344 are D/F/A. The protagonist arrives on
D5; its partner supports below it on F4, rather than leading another independent
closing stream. The final sustain lasts 5.5 beats, followed by 2.5 beats of score-end
silence. This is an earned-arrival hypothesis supported by the actual notes, not
a guarantee of perceived emotional/narrative success.

Verification: both source C's improved opening and liked bar 53 remain unchanged;
all notes outside the explicitly owned regions remain unchanged; tempo, total length
and non-note events are preserved; no unterminated notes; individual tracks match
the full-score notes; local job and MIDI download return HTTP 200. Five focused MIDI
serialization tests pass, including exact-score-end release ordering and composition
of disjoint revisions. No VST build, Git commit, Cloud deployment or shared default
change was made. The intermediate rejected/private drafts are not in Auto history.

### Candidate E: specific beat subdivision in bar 30

The listener identified the answering violin's G4 at Live position 30.3.2 as an
awkward isolated sixteenth displacement after F–G–A. This note was present in the
original and unchanged in C/D; it was not introduced by the climax/ending revision
or shifted by importing into Live. The suggested position 30.3.3 is absolute beat
118.5 rather than 118.25.

Local Auto job `c7ef3673-4757-4218-815d-4695fb6602f1` is AI-authored against D as
its immutable source. It preserves the eight pitches and velocities of the answer
at [116,128). The AI writes A4 at 117 for 1.25 beats and G4 at 118.5 for 0.75 beats,
leaving a short quarter-beat articulation before the following attacks. All other
notes of that phrase, all other tracks, D's climax and its revised ending remain
exactly unchanged. This tests the identified onset placement with contextual
articulation, not a global eighth-note quantization rule. It has not yet been
accepted by human listening. MIDI integrity and HTTP availability were verified.
