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
