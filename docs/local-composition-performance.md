# Local composition performance study

This is a measured development study, not a production latency promise. It
changes neither authored MIDI nor musical acceptance gates until an A/B result
passes the objective checks and a human audition.

## Baseline (2026-10-11)

- Request: 600 seconds, 120 BPM, 20 explicitly requested MIDI tracks; job
  `d8c6802c-420e-49de-bb5d-4e82a2d4fddf`.
- Completed in 48.0 minutes with 20 nonempty tracks and 3,465 notes.
- 71 API calls consumed 43.9 summed API minutes. Response-cache reuse made
  the initial macro/cast faster than a cold request.
- 13 planned writing blocks, 4 grouped-block tonal splits, 3 granular
  recoveries, and 16 logged compact repair episodes. The API trace contains
  33 performance-block calls and 27 compact-voicing calls. Writing alone
  consumed 1,732 seconds of remote wait and produced 189,710 output tokens;
  compact voicing consumed another 450 seconds. These are the dominant
  measured costs, not MIDI export, local rendering, or the two-second status
  polling cadence.
- The final MIDI passed technical and narrative publication gates, but the
  audit still reported 16 harmonic conflicts, 12 low-register conflicts and
  incomplete soundscape evolution. Completion is not proof of ideal music.

## Controlled experiment

`PULSO_AI_SINGLE_OWNER_WRITING=1` on a separate local server partitions each
established writing block into one-owner transactions. The owner order,
blueprint, authoring schema, MIDI normalization, tonal checks, checkpointing,
and final publication gates are unchanged. The hypothesis is that the extra
initial transactions cost less than repeated multi-owner splitting, missing
owner recovery, and rewrites.

The first trial was stopped at 18.6 minutes because the same prompt and seed
produced a different blueprint (A minor versus E minor), invalidating a
musical A/B comparison. Its partial checkpoint was retained. The controlled
trial instead sends `blueprint_from_job` to the local-only API. It verifies
prompt, duration, tempo, seed and mode against the source job, restores only
the exact accepted blueprint and **zero MIDI notes**, and asks the AI to
author every lane again. The new job's blueprint hash must equal the base
job's hash. This diagnostic path does not change ordinary generation or
checkpoint recovery.

The controlled comparison still has stochastic AI performance writing. A
single result cannot prove a typical speedup or identical musical quality.

The exact-blueprint trial `b17cdf18-2075-434a-8d5f-d6be2627d4ed` was stopped
after 18.0 minutes and approximately USD 1.53 estimated usage. Its blueprint
hash matched the baseline byte-for-byte, but after eight authored owners its
checkpoint had 29 unresolved tonal events. The baseline's eighth developed
owner had four. This does not prove the partition alone caused the difference,
because the AI writes stochastic notes, but it fails the conservative
no-regression criterion. **Single-owner writing stays opt-in and is not
promoted to the local default, VST or Cloud.** Both partial checkpoints remain
available for musical inspection.

## Decision and next safe experiment

Do not enable Fast service tier as the default shortcut. OpenAI documents
Fast for GPT-5.6 Terra, but its current rate is higher than Standard and it
would leave the 33 writing calls and 27 compact-voicing calls intact. A
per-request Fast A/B is possible later if latency becomes more valuable than
cost, but it is not the requested cost-neutral improvement.

The next implementation candidate is a *lossless* compact wire schema for
AI-authored note events: retain every pitch, onset, duration, velocity,
voice, owner, control event, placement and validation rule while reducing
repeated JSON key names. Keep the existing wire schema as the default until
exact-blueprint paired trials demonstrate lower output tokens and no worse
technical, tonal or musician-audition results. Do not infer musical parity
from JSON equivalence alone; the format can change model behaviour. If this
candidate fails, preserve the established path exactly as for single-owner
writing.

Official references: [latency optimisation](https://developers.openai.com/api/docs/guides/latency-optimization),
[Fast mode](https://developers.openai.com/api/docs/guides/fast-mode), and
[API pricing](https://developers.openai.com/api/docs/pricing).

Run `node tools/analyze-local-latency.mjs <job-id>` for the per-phase trace.
Run `node tools/compare-local-compositions.mjs <baseline-dir> <candidate-dir>`
after both jobs complete. Never promote the focused strategy solely from a
speed result; the candidate must retain all requested tracks, pass technical
and narrative gates, show no worse measured tonal conflicts, and be auditioned
by a musician.

## Lossless wire and Fast trial (2026-10-12)

The opt-in `PULSO_AI_COMPACT_WIRE=1` shortens only the repeated event keys in
performance-block responses. The parser expands every beat, duration, pitch,
velocity, voice, owner, metric intent and control event before the established
MIDI normalization and musical gates. It does not add, remove, move or rewrite
notes. `PULSO_AI_FAST_MODE=1` independently requests OpenAI Fast; the trace
records both the requested tier and the actual response tier. Neither flag is
enabled in the ordinary local server, VST or Cloud.

`node tools/measure-performance-wire.mjs` measured 67 cached standard-wire
performance responses containing 3,206 notes and 13 control events; it excludes
18 already-compact responses. Shorter event keys reduced normalized output JSON
from 1,128,977 to 971,480 bytes (14.0%). The tool verifies exact object-level
round-trip equality; the C++ test also verifies event decoding and owner range.

The exact-blueprint, same-prompt/seed, 600-second trial
`ca5e8920-c2eb-4d01-a0cd-c939f34c7a81` used **both** compact wire and Fast.
It completed with 20 active tracks and passed technical and narrative gates,
but took 46.0 minutes versus the 48.0-minute standard baseline, 80 API calls
versus 71, and about USD 8.20 versus USD 4.55. OpenAI reported `priority` as
the actual service tier on the Fast calls. The candidate had fewer measured
harmonic conflicts (3 versus 16), but its resolution score was lower (0.717
versus 0.881), it contained 2,803 versus 3,465 notes, and both versions still
failed the soundscape-evolution audit. These observations cannot isolate the
effect of wire encoding from Fast or stochastic note authorship. The 4% wall
time improvement and weaker resolution metric do **not** justify promotion.
Human audition remains pending.

A second exact-blueprint trial with the **original schema and Fast only** was
run as job `d793be6c-91fc-47d0-81e1-253095c8b569`. OpenAI reported actual
`priority` service on all traced calls. Its performance-block phase generated
212,249 output tokens in 1,636 API seconds, versus 189,710 in 1,732 API seconds
for Standard: about 16% less API time per output token, but more tokens and
more musical recovery erased that per-token gain. The first attempt lasted
42.9 minutes and cost an estimated USD 7.69, then failed the final musical
brief: only 19 of 20 voices were independently developed. Automatic checkpoint
recovery `cb564865-f2d7-435b-9675-b3e674f5a44e` added 4.5 minutes and about
USD 0.99, and failed the same brief. **User-visible outcome: no complete song
after 47.4 minutes and about USD 8.67.**

The measured decision is **do not enable compact wire or Fast by default**.
Neither created a meaningful end-to-end speed improvement on this controlled
long work, and Fast-only worsened publication reliability in this sample.
Compact+Fast created a technically valid alternative, but the change in
musical-resolution score and the stochastic authorship require human audition
before any quality claim. This is not evidence that Fast intrinsically lowers
musical quality; it is evidence that faster individual calls do not solve the
current write-review-rewrite bottleneck. The next optimization should measure
and redesign the number of paid AI revision cycles while preserving authored
notes, validation and checkpoint integrity; it must be evaluated with complete
works and human listening before activation.

## Follow-up screening: model, reasoning and denser wire (2026-10-12)

The ordinary local studio, VST and Cloud still use **GPT-5.6 Terra / medium,
standard service, original performance JSON**. The following trials used an
isolated local server and the exact accepted blueprint, prompt, 600-second
duration, 120 BPM and seed of baseline job
`d8c6802c-420e-49de-bb5d-4e82a2d4fddf`. They were intentionally stopped
once the same-stage comparison could not support a speed *and* quality claim;
an early stop is not evidence of final musical quality.

| Trial | Job | Observation | Decision |
| --- | --- | --- | --- |
| GPT-6.1 Sol / medium | `09834d88-be5f-433f-b576-253c54e4c8e3` | Block 1 had not passed after 5.7 min and six API calls; the Terra baseline passed it in about 3.4 min. | Stop; changing to the stronger model alone is not faster here. |
| GPT-6 Luna / medium | `5a9b4040-3df5-494d-b7b0-0acbf23670bf` | The first five blocks took 8.6 versus 13.3 min, but by block 10 the run took about 24.8 min and repeated tonal revisions had largely erased the lead. At that checkpoint: 14 parts, 4,306 rendered notes, 40 low-register conflicts; Terra's *finished* reference has 3,465 notes and 12 such conflicts. The run was stopped after the next block continued needing revisions. | Faster first drafts do not establish a faster, musically comparable completed work. Do not promote. |
| Dense event wire / Terra medium | `620deb81-04ae-412b-bd94-2ce332cfeb0a` | First performance response was valid and used 6,119 output tokens versus 7,369 for the reference. Eight calls and 4.7 min still had not accepted block 1, versus seven calls and 3.4 min for the reference. | Stop; fewer event bytes did not remove the repair bottleneck. |
| Terra / low for writing | `457e4732-d0c1-4841-8c73-a6374ae4de62` | After seven calls and 3.5 min, block 1 was still unaccepted. Those seven calls used 7,110 reasoning tokens versus 2,831 in the reference's seven calls. | Stop; requesting lower effort did not reliably reduce actual work. |

The dense format is a **transport-only** opt-in: every note still carries its
AI-authored onset, duration, pitch and velocity plus an index into the exact
instrument cast; expression controls carry their authored timing, controller
and value. Source voice and `strict_grid` are immutable properties already
known from that cast, and the existing parser restores them before MIDI
validation. A unit test checks the strict owner/register schema and exact
note/control round-trip. Across 80 cached, standard-format performance
responses with 3,780 notes, this representation would reduce normalized JSON
bytes by 29.2% (versus 13.7% from short keys alone). Byte savings cannot be
equated with output-token or end-to-end latency savings, and a format change
can alter what the AI composes. It remains disabled by default.

The baseline's 48.0-minute wall time comprises about 43.9 minutes inside
71 API calls. Performance writing alone took 28.9 API minutes across 33 calls;
compact voicing and final selective repair took another 12.5 API minutes
across 32 calls. Local rendering, polling and validation account for only
about 4.1 minutes of the wall time. Background polling already checks every
two seconds. Thus faster HTTP handling, browser rendering, shorter key names
or a low-effort setting cannot plausibly deliver the several-fold improvement
needed for a production-length work.

**No candidate in this follow-up is validated as faster without musical
regression. Nothing was enabled in the normal product.** The next architectural
experiment should measure a dependency-aware conductor: finish the harmonic
and protagonist anchors first, let genuinely independent supporting voices be
AI-authored against the same immutable anchors in bounded parallel batches,
then run the existing full-score MIDI audit and focused AI revisions. It must
preserve every authored note and the explicit cast, and be rejected if it
increases conflicts, leaves voices undeveloped, or fails musician listening.
An early playable checkpoint can also reduce *perceived* waiting, but must not
be reported as reducing full-composition time.

Experimental flags are local-only and off by default:
`PULSO_AI_EXPERIMENT_MODEL=gpt-6.1-sol|gpt-6-luna`,
`PULSO_AI_EXPERIMENT_EFFORT=low`, and `PULSO_AI_DENSE_WIRE=1`. Use each on an
isolated studio process and record job IDs; never enable one in Cloud or the
VST on the strength of a partial run. OpenAI model availability and pricing
were checked against the official
[model pages](https://developers.openai.com/api/docs/models) and
[pricing](https://developers.openai.com/api/docs/pricing).

## Five-minute joint-window recovery trial (2026-10-12)

The isolated `4183` studio uses coordinated ensemble windows, with a hard
five-minute/12-track ceiling. It never silently switches to the normal writing
engine when that ceiling is exceeded. At the time of this 2026-10-12 trial,
the normal `4179` studio, Cloud and VST remained unchanged; this sentence is
historical, not the current deployment state.

A 300-second, 12-track D# minor request failed at window 8/11 after one-owner
revisions repeatedly moved conflicts between the bass, beds and melodic
voices. Job `b67d9999-c27c-4d1b-988a-e8b859fbeafc` preserved the first
seven windows (1,092 notes) but spent an estimated USD 1.55 in 20.4 minutes.
An exact checkpoint replay `9f7bcbbd-5660-47f1-b36e-8b85d8164654` still
failed at window 8 after seven more paid calls (USD 0.24); the same single-owner
strategy could not solve a multi-voice conflict graph.

The local recovery now selects **both sides of every sustained collision**,
plus the owners of unsupported or structural notes, for one bounded AI revision.
A broad conflict graph returns the entire *unaccepted* window to the AI. The
request includes a bounded map of actual sounding conflicts and representative
rejected phrases; previously accepted windows remain immutable. The AI still
authors every replacement pitch and note, and the normal render/MIDI/tonal
checks must approve the complete passage before it is checkpointed. Synthetic
tests cover pairwise, unary, missing-owner and broad-graph selection.

Exact-checkpoint replay `da0b5603-3955-41da-8c67-34ff3a6ac67f` passed
window 8 and completed all 11 windows. The final "Eje C#" is 152 bars, 12 tracks
and 1,738 direct-AI notes; its technical audit has zero harmonic conflicts,
unsupported chromatic notes and invalid sustains. That replay took 16.7 minutes
and an estimated USD 1.30 across 23 paid calls plus seven locally reused calls.
This is a successful **failure-recovery and technical-validity** test, not yet a
claim of production-ready musical quality: the final audit still reports
`narrative_ready=false` and `soundscape_ready=false` (disconnected melody and
insufficient evolution in some electronic layers). Human listening and at least
one clean full run remain required before promotion to the ordinary studio.

An unrelated two-minute orchestral request (`5a3e9893-8507-4377-bc54-cb2f915dc671`)
then exposed a cast-boundary defect: the AI authored ten instruments, but the
general orchestral normalizer appended three generic department/coverage
owners and the twelve-track experimental writer correctly refused to proceed.
The request stopped before performance MIDI after three planning calls and
about USD 0.18. The experimental writer now caps the structured cast manifest
at twelve entries and carries an explicit `preserveAuthoredCast` policy through
normalization: derived planning metadata may still be normalized, while no
unrequested instruments are appended to this trial's AI cast. A unit test
checks a closed orchestral cast survives normalization unchanged. A second
two-minute request (`ecd17f2a-61f8-4ad2-889f-2760ea4a6eac`) still failed:
the accepted AI blueprint again contained ten instruments, but the parser's
*internal* normalization expanded them to fifteen before the caller set the
closed-cast flag. This cost about USD 0.12 and produced no performance MIDI.
The closed-cast policy is now passed into the parser and preserved across its
SongPlan reset, before its first normalization. A parser-level regression test
and an offline replay of that exact accepted blueprint both preserve all ten
authored owners and their order. The corrected worker is running on `4183`;
this specific cast-boundary fix has not yet been tested with another paid
composition, and it does not establish musical quality.

## One-minute tonal-narrative trial (2026-10-12)

The isolated `4183` writer completed job
`8ec7b7c0-526d-463a-8ac0-7d5cd3e8dc8c` ("La pregunta de Re") at 120 BPM:
30 bars, eight AI-declared instruments and 235 notes, all marked `direct_ai`
with no generated or transformed notes. The MIDI has no invalid sustains,
unsupported chromatic notes or reported harmonic clashes. Its accepted macro
plan names a question in D minor, an answer, a climax and a final return.

This is **not yet a validated musical optimum**. The protagonist plays 15
notes in the premise, none in the second act, ten in the climax and ten in the
resolution. Its opening D-F-E-A attack pattern returns almost literally near
the climax and again at the ending; the MIDI audit reports
`narrative_events_lack_audible_consequence` (`causal_narrative=0.5625`). The
flute has only two notes despite being counted as an instrument, and only six
of eight voices are classified as meaningful. A musician still needs to hear
whether the answer and final cadence make that recurrence feel intentional.

This 60-second work required 24 paid API calls, 21.4 minutes and an estimated
USD 1.42. Most of the time was spent on AI revisions of vertical harmonic
clashes within the ensemble windows. The cast was smaller than the preceding
ten-owner failed trial, but the full-ensemble revision strategy still
oscillated between small conflicts. **Do not enable this experimental writer
in the ordinary studio, Cloud or VST on the strength of this result.** The
next comparison should preserve the accepted macro blueprint and test a
smaller AI-authored anchor group (protagonist, chord bed and bass) before
supporting voices are authored against its immutable MIDI. No procedural
note-writing or automatic musical-fill operation is proposed.

That controlled A/B comparison is job `c506f730-9181-4726-95a4-d24b122d6a1c`.
It reused the exact accepted macro blueprint, prompt, seed, duration and
eight-instrument cast from Anclaje 1; only the writing order changed. The
first group authored protagonist, chord bed and bass, then the AI authored
the companions against their accepted MIDI. It completed in 18.7 minutes of
writing (planning was reused), with 22 paid calls and about USD 1.22.
Its MIDI contains 419 direct-AI notes, versus 235 in Anclaje 1. The local
audit marks all eight layers meaningful and its coarse causal-narrative
metric rises from 0.5625 to 1.0, but motif closure falls from 0.9825 to
0.605, resolution score from 0.883 to 0.750, and the audit reports
`disconnected_melody_without_voice_leading`. The increase in notes is not
proof of richer music; human A/B listening must decide. There is no measured
speed benefit after excluding the reused planning phase, so this grouping
is **not promoted**. A further isolated trial will ask the AI to cast a
compact, genuinely developed ensemble for one minute rather than filling
an eight-owner roster. It does not prescribe note content.

Human A/B listening favored the split-anchor version as the **better musical
story**, despite its greater note count and weaker numerical motif-closure
score. Preserve both exported MIDIs: `experiments/anchors/01-la-pregunta-de-re`
is the first accepted reference and `experiments/anchors/02-la-pregunta-de-re-dialogo`
is the favored A/B result. The listening result is meaningful evidence for
musical direction, but it does not resolve the latency or repeated-revision
cost; keep both architectures experimental until a repeatable shorter-form
and longer-form comparison is complete.

The compact-cast follow-up (`cbaa0cf6-c37c-49c9-8ed4-1a0a07cfb346`)
validated a five-instrument AI cast after fixing the experimental structured
schema's hidden eight-instrument minimum. It was stopped after accepting only
two of four windows: about 20.1 minutes, 22 paid calls, estimated USD 1.25,
and 36 recovery events. This is a negative efficiency result, not evidence
that five instruments are inherently less musical. Most time was spent on
revising simultaneous harmonic collisions. The partial MIDI and progress
trace remain in the isolated job directory; do not present them as a song.

The trace also exposed a structural false positive in the tonal audit: the
root/diminished-fifth tritone of an explicitly declared half-diminished chord
(E-Bb in Em7b5) was reported as an unintended collision even in a mid-register
voicing. The third/sixth tritone of a declared minor-sixth chord (Bb-E in
Gm6) had the same problem. The local code now recognizes those functional
pairs while still rejecting their low-register equivalents. This changes
validation, not MIDI authorship; it requires regression tests and a fresh
controlled comparison before any Cloud/VST promotion. User listening still
favors the split-anchor A/B result as the current musical reference.

A fresh no-local-response-cache replay of the same accepted blueprint and
split-anchor writing order (`ab586af1-c80c-4644-8177-a9e86cd1ac78`) completed
after the functional-harmony audit change. It produced 415 direct-AI notes
in eight tracks, with no exact eight-bar note repeats and no unterminated
notes. It took 22.2 minutes, 30 paid calls, 43 recovery events and an
estimated USD 1.47. The previous listener-favored split-anchor run took
18.7 minutes, 22 paid calls and USD 1.22. Thus recognizing the declared
half-diminished and minor-sixth colours fixes a real false positive but does
**not** solve the end-to-end latency or cost. In the new trace the E3-Bb3
half-diminished pair is no longer reported; low-register E2-Bb conflicts
and other real chromatic/voice-interaction issues still are. The final audit
marks narrative ready but flags weakly developed soundscape voices, while
the earlier preferred run was soundscape-ready. Do not replace the favored
A/B anchor or promote this local experiment; await human listening to the
new MIDI and redesign the ensemble coordination before further costly runs.

## Current musical anchor: Pregunta suspendida (2026-10-13)

The musician preferred the complete three-minute `Pregunta suspendida, Re menor`
to the previous `The Rest That Returns` comparison. The exact accepted MIDI,
request, manifest, blueprint, plan and audit are preserved in
`experiments/anchors/03-pregunta-suspendida-re-menor`. This is a listening
anchor, not a claim that every future prompt will match its quality.

The selected architecture uses AI-authored coordinated ensemble windows and
narrative anchors. `pulso_cloud_worker` is shared by Local Studio and Cloud;
Cloud explicitly enables the same editorial, joint-window and narrative-anchor
flags for the bounded short-form architecture. Cloud deliberately exposes only
prompt, duration and tempo; the local studio retains experimental controls.
For durations outside the joint-window ceiling, the two interfaces are not an
identical experiment and should not be described as output-equivalent.

Cloud now preserves validated AI checkpoint notes and their blueprint in a
worker-readable resume file. The backend can retry only a controlled worker
failure from such a checkpoint, within its recovery budget. No notes are
procedurally invented for this recovery. The `Tus obras` Cloud library has
also been aligned with the useful local layout: title, status, date, duration,
track count and explicit refresh.

The deployed Linux worker was smoke-tested on the public server with a new
60-second request using the same prompt family and seed, without reusing the
anchor's notes. It completed with 9 tracks and 537 direct-AI notes; MIDI
rendering, technical/narrative audit and final-score review all completed.
The audit marked `technical_ready=true` and `narrative_ready=true`, but
`soundscape_ready=false` and reported two unsupported chromatic events.
This validates the deployed path, not flawless musical quality or shorter
latency: the smoke run took roughly 13 minutes. The accepted three-minute
MIDI remains the actual listening anchor.
