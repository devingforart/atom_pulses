# Performed gestures and expressive planes (local experiment)

## Hypothesis and scope

The listener rejected the expectation-led trial as too constant. Its first and
second violins retained strongly quarter-note-based motion across the middle,
despite prose about consequence and contrast. Do not present another wording
change as an established musical improvement.

This experiment changes the initial writing representation. `AiGestureScore`
requests each selected voice as an array of complete performed gestures, rather
than arrays of notes partitioned by harmonic section. Each gesture has an
absolute `start_beat`, concise `character`/`relationship`, and all its actual
notes with gesture-local `beat` and sounding `duration`. The AI chooses the
gesture boundaries and can cross sections. There are no motif references,
repetition counts, automatic ornaments, density targets or rhythmic templates.
The writer is asked to distinguish expressive weight, supporting texture and
motion in the actual notes, rather than add fast notes everywhere.

The parser performs coordinate translation only:
`absolute_attack = gesture.start_beat + note.beat`. It routes that attack to its
existing score section without changing pitch, velocity, attack, duration or
voice. Invalid coordinates are rejected, not repaired, quantized or clipped.
The normal direct-AI score/rendering path remains the downstream authority.
Cross-section note preservation is tested with explicit fractional durations.

The existing owner-scoped AI rehearsal still uses the established explicit
window edit format. It receives read-only per-owner/per-section onset-gap
observations to help inspect rhythmic inertia; those numbers never reject or
rewrite music. The notes, not the gesture descriptions, are reviewed. The
expectation-led review prefix now explicitly describes an evaluation context
and the `keep` empty-window contract, not another composition request.

Structured schema implementation follows the official [OpenAI structured-output
documentation](https://developers.openai.com/api/docs/guides/structured-outputs).
Schema adherence and technical integrity do not establish musical quality.

## Controlled trial

- Baseline: `b1a0271b-1246-47f6-bb4e-aa53e24a7200`.
- Candidate: `e754f6da-39ee-41ad-9da6-af5ede25e571`.
- Prompt `Musica como la de mozart.`, 60 seconds, 120 BPM, adaptive,
  AI sovereign, seed `202610200201`, first-pass with phrase architecture.
- The exact accepted blueprint and phrase references are retained. No performed
  MIDI is imported. New actual notes are generated in the alternative format.
- Flag `PULSO_AI_GESTURE_SCORE_EXPERIMENT=1`, requiring expectation-led whole
  argument mode. Launcher `node web/local-studio/start-gesture-score.mjs`, 4210.
- Standard/Cloud environments strip the flag; no production promotion, VST
  installation, commit or deployment is part of this experiment.

The C++ authored-note preservation and bounds checks, including a gesture that
crosses a section, passed. The 19 Node environment-isolation checks passed.
Musical results must be recorded from actual notes and listener feedback.

## First attempt not selected

`e754f6da-39ee-41ad-9da6-af5ede25e571` completed but is NOT a musical promotion.
The AI still aligned most gesture starts with the visible section boundaries,
and its principal line mainly used quarters and halves after the opening.
Its rehearsal identified a sustained C-sharp problem before the applied dominant
and a questionable closing bass, but emitted only two windows for a globally
owner-scoped revision. That incomplete replacement was correctly not applied;
the original was kept. Do not describe the trial as musically repaired.

The next iteration hides section boxes from initial gesture writing while
retaining a continuous absolute harmony timeline. It clarifies that expressive
weight is not uniformly longer notes. Review uses a nested discriminated
keep/revise schema: keep has empty arrays; a revision must contain all windows
for its selected owners, with the full original safety checks still applied.
This is a protocol-capacity fix, not an automatic musical correction. An AI may
still choose to keep successful music or write a poor replacement; listening
and note inspection remain necessary.

## Subsequent trials and technical findings

- `d473f9a9-14ea-4764-aeaa-74f6de54a92d`: controlled Terra/medium comparison,
  same accepted blueprint/phrase references as the baseline. Completed with 468
  direct-AI notes. Every authored gesture note reached the first MIDI unchanged;
  the scoped rehearsal replaced only cello notes, and the selected five-voice
  argument survived orchestration exactly. The full arrangement gives the harp
  more moving figures against broader violin utterances, but the reduced score
  remains quite regular. This is not proof of a compelling narrative.
- `86941013-c609-48c2-9dc7-cf8019a649ea`: fresh Terra/medium work, seed
  `202610200302`, `The Suspended Answer`. Completed after a technical retry for
  an out-of-bounds release. A same-owner/same-pitch attack at absolute beat 60
  appeared twice with different attributes; existing score normalization merged
  it. Raw-to-MIDI conservation therefore failed for one viola event. Do NOT
  claim this trial is lossless or select it as the anchor. The gesture parser
  now rejects such ambiguous unison attacks with exact feedback, rather than
  allow them to be silently merged. This validation does not generate music.
- `8972f814-030b-447d-8e36-8519b5cd9551`: explicitly announced isolated
  Sol/medium comparison, same blueprint/phrase references, prompt and seed as
  the controlled Terra trial. No production model default was changed. The
  existing local experimental model override is used, not a new routing rule.

The Sol experiment can be restarted explicitly with
`node web/local-studio/start-gesture-score-sol.mjs` (4212). Its trace confirms
`gpt-6.1-sol` / `medium`. The Terra experiment remains available via
`node web/local-studio/start-gesture-score.mjs` (4210). These launchers do not
edit the compiled production model configuration.

Earlier audition links used `argument=1`, which deliberately omits the unselected
accompaniment. That is useful for inspecting the melodic argument, not a fair
audition of the full textural hierarchy. Final candidate links must open the
full arrangement by default, with reduction available as an optional comparison.

## Selected audition candidate (not a production promotion)

The Sol trial `8972f814-030b-447d-8e36-8519b5cd9551` completed at 23:40:32 ART,
2026-10-20. It is the same 60-second / 30-bar G-major macro and cast as the
controlled Terra trial. The actual API trace confirms Sol/medium. The selected
full arrangement contains 561 direct-AI notes in eight sounding tracks, compared
with 566 in the earlier expectation-led baseline: this is not an increase in
note count offered as a substitute for development.

Observed differences in the realized notes:

- Principal violin: 115 notes, including 13 sixteenth-note durations, 47 eighths,
  longer supports and several dotted/mixed proportions. The earlier baseline
  had 95 of its 123 violin notes as quarter notes. The Sol result includes brief
  moving connections and broad arrivals within the same melodic line.
- Gesture boundaries for the principal/answering voices no longer all coincide
  with the four section starts. These are AI choices, not shifted timestamps.
- Opening D–G–A–B identity is recognizable in the score; at 00:34 the transformed
  ascent meets the replying line; from 00:50 the contour returns before the
  final G at 00:58, held through 01:00. These are score-reading landmarks, not
  a report of acoustic listening.
- The AI rehearsal chose keep. No melodic replacement was applied. All 315
  foreground notes survived accompaniment with unchanged pitch, onset, duration
  and velocity. Raw gesture-to-first-MIDI conservation also passed for every
  owner, and no within-track same-pitch overlaps or unterminated notes were found
  in the full MIDI.

The candidate is selected for human listening because of these concrete changes,
not because its rehearsal praised itself or because greater rhythmic variety
proves emotional depth. The controlled Terra trial used the same revised format
and references but remained more uniform. One comparison does not establish a
general model-quality ranking or prove the representation caused superiority.
Do not credit a prompt-only change for this result: the model also changed.

Full audition: `http://localhost:4212/?job=8972f814-030b-447d-8e36-8519b5cd9551`.
Original full baseline: `http://localhost:4212/?job=b1a0271b-1246-47f6-bb4e-aa53e24a7200`.
The optional argument-only view adds `&argument=1`. HTTP checks returned 200 for
the page, selected reduction and full MIDI. No browser/audio listening tools were
available, so perceptual quality still requires the listener.

The worker was relinked with the duplicate-attack integrity guard after the Sol
job ended; that candidate already passes the guard's uniqueness condition and
the raw conservation audit. The C++ preservation/scope tests and 19 Node
isolation tests passed. Auxiliary 4211 was stopped after becoming idle; its
works remain in the shared library. No production model/default was changed,
no VST was rebuilt or installed, and no commit, push or deployment was performed.
