# Ensemble discourse: local listening experiment

Listening outcome (2026-10-20): the user did not find **Bearing Stone,
Returning** sufficiently coherent. It is not an accepted anchor. Its
technical checks and symbolic evidence below remain observations, not
proof of musical quality. Follow-up: `docs/thematic-subject-study.md`.

Production remains at accepted commit `78fdaf2`. No deployment or profile
promotion is part of this experiment. The shared profile is unchanged.

## Source evidence

The production-worker smoke composition **Bearing Tone, Open Question**
contains 470 AI notes, eight parts and 60 seconds at 120 BPM. The exact
original is copied into local Auto job
`2ef024e2-e9dc-49e0-a3b6-25240d688044`; the original server artifacts are
untouched. Most melodic notes are connected, so shortening alone does not
explain the listener's report of agitated or unclear discourse.

In bar 12, violin attacks at beats 0, 1.5, 1.75, 2, 2.5 and 2.75;
cello at 0, 1 and 1.5; clarinet at 0, 1.5 and 2; harp at 0, .5 and 1.
The inner voice and bass add their own motion. These are not automatically
errors: the hypothesis is insufficiently audible foreground and shared
prosody, not an illegal number of voices or a ban on rapid counterpoint.

## Experiment

`PULSO_AI_ENSEMBLE_DISCOURSE_EXPERIMENT=1` extends the existing AI phrase
plan with phrase-local `ensemble_spans`. The AI chooses each span's extent,
foreground speakers, pulse carrier, inherited listening thread, arrival
time and musical goal, plus the relevant instruments' contribution and
relationship to the foreground attacks. Multiple foreground voices and
intentional ensemble breaths remain available. All choices are AI-authored.

`AiEnsembleDiscourse` validates only data representation, bounds and cast
identity. It supplies the shared listening plan to the existing note writer,
which authors every onset, pitch, duration, rest and accent. There is no
post-generation thinning, rhythmic template, maximum moving-voice count,
fixed note duration, automatic retuning or new musical rejection gate.
Cross-window instructions retain their full coordinates; only the textual
context is selected for the relevant window. MIDI is never clipped here.

The first comparison retains the original macro harmony, cast, prompt,
duration, tempo and seed while replanning phrases and writing a new score.
It is a controlled request, not a note-identical backing-track comparison:
independent AI realizations remain stochastic. A new seed is needed to
assess generalization before proposing production promotion.

## Context fidelity follow-up

The accepted production voice memory retains four opening notes and eight
recent notes. That may omit the middle of the original statement while
preserving its small signature. The experimental follow-up supplies the
earliest completed AI-declared phrase's actual notes for its speakers and
respondents, including durations and velocities. Normal phrases are shown
in full; unusually large phrases use an explicitly labelled first/last
48-note context bound. This is historical prompt context, not a renderer or
an instruction to copy the phrase. Existing recent-note context remains.
All AI-declared foreground partners are included in the coordinated writing
group; there is no new hard-coded instrument assignment or voice-count cap.

The initial trial's first plan was rejected for listing the same horn twice
in a span's textual instructions. Provider response inspection confirmed
valid cast identity and two nonempty instructions, not duplicate MIDI.
The experimental representation check now accepts compound/repeated owner
instructions; it still rejects nonexistent instruments and invalid bounds.
A unit test covers this distinction. The initial running binary is retained
under its recorded hash; later changes do not alter an in-progress score.

`tools/compare-musical-discourse.mjs` offers a blinded advisory comparison
of actual MIDI, with randomly assigned X/Y labels, precise onset citations
and citation checks. It does not receive either method's planning prose,
edit notes, reject a work or claim to have listened to audio. The critique
is secondary evidence alongside manual score reading and human listening.

## Complete-dialogue-first trial

The initial hierarchy-only realization (`f94df22d-1a60-4aab-a0e6-ffdc4c72be44`,
388 notes) and memory follow-up (`9b34268b-9275-491c-ac3f-6bf0b11152b9`, 380
notes) completed technically but introduced larger opening-line gaps. In
the second trial the violin releases E5 at beat 4.5 and next attacks at 14.
This is not selected as a demonstrated musical improvement. The first
blinded critique preferred the original but included inaccurate note
citations/pitch descriptions; its high confidence is not authoritative.

The next experiment changes authoring order rather than appending more
coordination prose. `PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT=1` uses the AI's
declared melodic speakers/respondents to author complete dialogue across
adjacent sections in one bounded request. Accompaniment is then authored
against the complete, immutable actual foreground, including its future
notes inside the relevant windows. There is no generated motif expansion,
automatic melody duplication or post-render arrangement thinning.

`AiCompleteDialogue` packs at most 48 bars per request as a representation
bound, not a musical cadence schedule. Each AI note stays in its authored
section/window coordinates and passes the existing direct MIDI parser.
Long works retain their complete harmonic/narrative blueprint and receive
actual opening/recent melodic context between payloads. Musical quality
and stability on long works are not established by a one-minute trial.

An accepted full-dialogue checkpoint is explicitly provisional with zero
completed accompaniment windows and `prewritten_dialogue=true`. It is not
published as a completed arrangement. Such checkpoints can only resume
with the matching experimental worker; older partial-window scores are
not silently treated as complete dialogue. The checkpoint is emitted only
after the whole foreground phase passes its technical checks. Recovery
inside a partly completed multi-request foreground phase remains bounded
per request; no production promotion is warranted without longer trials.

Local launcher: `node web/local-studio/start-complete-dialogue.mjs`, port
4195. Ports 4193 (accepted anchor) and 4194 (hierarchy study) are unchanged.

## Single numerical authority follow-up

The first complete-dialogue trial (`1d2906c0-3087-4c9a-9a9b-b00059cd6997`)
preserved its 106 foreground notes exactly during accompaniment, but held
B natural over a B-flat-major interval. The next trial supplies the actual
normalized harmonic timeline in window-local coordinates rather than the
raw blueprint. This preserves the requested tonal policy, including
intentional passing tones and declared chromatic colour; it does not retune
MIDI. Trial `206bad1a-9b64-4b1f-b6f1-a029af3efe00` completed with 462 notes,
but overlapped a repeated foreground D5 at beats 111.5 and 112.

Clarifying repeated-key release in the authoring instructions removed that
specific overlap in `060799aa-7149-43c4-b1e0-2cc9435dfca9` (486 notes), but
the AI still appended a short same-pitch pickup before a planned arrival.
This exposed two competing numerical scores: the sketch's scheduled
bearing tones and the actual melody being written.

The current experiment therefore keeps the sketch's semantic intentions
but omits its numerical bearing-tone coordinates from the writer's prompt.
The persisted plan is not modified. The actual complete dialogue is the
single numerical authority; accompaniment sees its real notes. This is
context and authoring-order work, not algorithmic smoothing or thinning.

Controlled trial `2c3b31ba-4c8d-474d-addc-02b1dbb8cf63` completed with 407
direct-AI notes across eight parts in 60 seconds. Its 132 foreground notes
(81 violin, 51 cello) remain identical in onset, duration, pitch and velocity
between `partial-01.mid` and the completed MIDI. The original has 127
foreground notes and 470 total notes: this trial does not obtain clarity by
emptying the main dialogue. There are no unterminated notes or rapid
same-pitch reattacks within a track in the advisory audit. Both melodic
voices release at beat 116; the harmonic body releases at 118, leaving one
second of terminal silence. These are factual observations, not a quality
score.

The blinded symbolic comparison preferred this trial over the deployment
control with medium confidence. Exact part identities and cited attack
times were verified. This check does not verify natural-language musical
claims: for example the critic calls an E a leading tone incorrectly.
Neither this preference nor technical validity substitutes for listening.
Remaining risks include a very high violin climax, chromatic colour against
the surrounding harmony, and echoes that may be less clear through one
piano. A fresh independent generation is required before generalizing.

Fresh-generation check: `631ed738-712e-4b9b-970c-f461efba3e81`, same prompt,
60 seconds, 120 BPM, seed `4203976088674690913`, no reused blueprint or phrase
plan. Macro form and cast succeeded, but both phrase-planning attempts failed
on numerical bearing-tone coordinates that this method no longer uses.
No score was published as complete. This exposed a remaining mismatch
between the planner's representation and the new complete-dialogue author.
Production and its shared default profile remain unchanged throughout this
study.

Experimental worker SHA-256 for this comparison:
`CA97332FA1086C8EF1F0C1A7A0B0AF40F5EB5B5797C351B4D81C49202C61A58A`.

The follow-up makes the experimental planner's strict schema request a
bounded `bearing_intentions` string list instead of `structural_tones`.
The other concrete phrase/delivery fields remain. Ordinary production still
requests and validates its original numerical sketch. Legacy plans remain
readable and their coordinates still receive the original representation
checks; the new semantic plans have no phantom note coordinates to reject.
Invalid text/list representation is still rejected, and actual generated
MIDI retains its existing technical checks. Tests cover mode isolation and
both valid and invalid semantic representation. The failed fresh job's
accepted blueprint/cast can be reused to test this change without paying
to compose that same macro plan again.

## Faithful sustained-note representation

Semantic-planner trial `62451de7-2f5e-41fa-bf1d-031ec21832c6` accepted its
phrase plan on the first call. Both complete-dialogue attempts then placed
a one-beat-or-shorter note across a section boundary by half a beat. The
section-local parser rejected these. Simply loosening that parser would
not be faithful: `replaceChunk` also clips legacy note releases to the
section boundary.

The follow-up adds an explicit `allowCrossSectionRelease` placement property
for complete-dialogue direct notes. Attacks remain assigned to their source
window; the original sounding duration can cross sections without a new
attack or a split note. Cell normalization preserves that authored tail;
the sovereign renderer bounds it by the whole work instead of its source
section. Checkpoint import preserves the same release under the explicit
`prewritten_dialogue` mode. Ordinary clips retain their previous section
boundary behavior. This is notation/serialization fidelity, not a new
musical rule, duration extension or note-generation procedure. The parser
still rejects releases beyond the whole work. Tests exercise exact onset,
duration, pitch and velocity through normalization and sovereign rendering,
as well as rejection of work-end overshoot and legacy-policy isolation.

Follow-up job: `4405195e-306d-40bc-9cea-c55ed58a01bb`, **Bearing Stone,
Returning**. It retains the independently generated macro/cast from the
failed fresh-seed check and replans semantic phrase intentions. Prompt,
duration, tempo and seed are unchanged. Experimental worker SHA-256:
`8B317A0EF2D62A1C1C4B7D806B5C857A68972F1C196F7E999B3C64AD6A3EC036`.
Both the semantic plan and complete foreground passed on their first call.
This is a continuation of the fresh-plan check, not a claim that its
macro/cast was regenerated a second time or that long-form listening passed.

This follow-up completed: 60 seconds, eight parts, 444 direct-AI notes.
Its 80 violin and 40 viola notes remain exactly unchanged in the final
arrangement. The read-only audit found no unterminated notes or rapid
same-pitch reattacks within a track. There are no sub-200-ms notes in this
realization, which is an observation, not a minimum-duration requirement.
The foreground has a traceable D-G-F-E/G proposition, its lower-register
counterargument and a C-sharp/D cadential return. A denser convergence
around 40-45 seconds still needs listening, as does the brief B/B-flat
suspension around 24 seconds. The last violin D lasts 1.75 seconds and
releases at the score end; zero score-end silence is not by itself proof
of an abrupt or successful ending.

Completed-score resume check `1fabc1ed-0901-479e-ba06-910aab88d037` made no
new API calls and reproduced all 444 final MIDI notes exactly, comparing
part identity, onset, duration, pitch and velocity. The synthetic render
test proves exact cross-section sustain preservation; this particular
live realization chose no cross-section sustains, so it does not alone
exercise that path. All three C++ test suites and 14 local parity/checkpoint
tests passed after this change. No VST or production deployment was built.

The second paired model critique preferred this follow-up, but it mixed
parts/scores inside some prose observations and misnamed an octave despite
using valid cited coordinates. Consequently its high confidence is not
accepted as evidence of musical superiority. Coordinate validation is not
semantic fact-checking. Candidate selection relies on inspection of the
actual MIDI; human listening remains pending. Both completed candidates
are preserved in Auto, with direct links:

- Controlled revision: `http://localhost:4195/?job=2c3b31ba-4c8d-474d-addc-02b1dbb8cf63`
- Independent-plan candidate: `http://localhost:4195/?job=4405195e-306d-40bc-9cea-c55ed58a01bb`

These changes remain uncommitted local research, not a replacement of the
accepted five-minute listening anchor or the deployed Cloud defaults.

## Running the hierarchy-only study

Build only the isolated worker and its tests in `build-ensemble`, then run:

```powershell
node web/local-studio/start-ensemble-discourse.mjs
```

URL: `http://localhost:4194`. The accepted laboratory on 4193 remains intact.
The local UI identifies this as a voice-coherence experiment. The key is
inherited privately from the existing environment and is never saved here.
The experiment flag cannot enter ordinary Cloud/local production defaults.

The representation uses strict structured outputs following the
[official schema guidance](https://developers.openai.com/api/docs/guides/structured-outputs).
The separation between representation tests, read-only musical evidence
and listening comparisons follows the distinction emphasized in
[evaluation guidance](https://developers.openai.com/api/docs/guides/evaluation-best-practices).
Neither schema validity nor a model critique establishes musical quality.
