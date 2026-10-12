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
