# Expectation-led whole-score writing (local experiment)

## Reason for the experiment

Listener feedback on material-dialogue trials: tonal and connected, but constant,
without consequential surprise, expressive development or a compelling story.
The 176-second trial also made its bassoon durations almost entirely uniform.
Do not promote it on the strength of more notes, planned states or an approving
AI rehearsal diagnosis.

## Change in the generator

`PULSO_AI_EXPECTATION_LED_EXPERIMENT=1` requires whole-argument mode. It replaces
the accumulated whole-score artistic instruction blocks with one concise
expectation/continuation/consequence instruction. It does not append another
checklist or add schema fields. Exact owners, pitch-class harmony, coordinates,
subject notes and any secondary note references remain available to the writer.
The main writer does not receive the old phrase-by-phrase narrative prose,
bearing-tone timestamps, section-function labels or motif/debt prose. Those are
retained in artifacts for comparison, not treated as commands for the actual notes.
The immutable harmony, cast, duration, grid and technical MIDI constraints remain.

The AI still authors the complete selected reduced score in one response, then
may keep it or make an explicitly owner-scoped rehearsal edit. The accompaniment
follows the actual selected notes, not the old phrase narration. Existing
whole-score musical context remains in the writing pipeline. No numeric rhythm,
motif, novelty, silence or density quota is introduced. No software transforms
pitches, rearranges rhythms, deletes notes or manufactures cadences.

This changes conditioning/context, not the underlying generative model. It is a
hypothesis, not an assertion that a concise instruction guarantees good music.
It follows the separation of instructions and context discussed in the official
[OpenAI prompting guidance](https://developers.openai.com/api/docs/guides/prompt-engineering).
No model change or production promotion is part of this experiment.

## Controlled one-minute trial

- Baseline: `349e82c8-083d-41f0-987c-6c524a1cd946`,
  `The D Question, Answered in G`.
- Candidate: `b1a0271b-1246-47f6-bb4e-aa53e24a7200`.
- Prompt: `Musica como la de mozart.`
- Duration 60 seconds, 120 BPM, adaptive, seed `202610200201`, AI sovereign,
  first-pass audition with explicit phrase architecture.
- Only the baseline macro/cast blueprint is reused; phrases are replanned and
  every performed note is authored anew. This is not an edit of baseline notes.
- Launcher: `node web/local-studio/start-expectation-led.mjs`, port 4209.
- The flag is stripped from standard and Cloud worker environments. Disabled
  mode preserves the earlier writer prompt byte-for-byte in the fixture check.

The listener should compare recognition, consequential continuation, perceived
movement and earned closure, not numbers of themes or duration values. Actual
note inspection can identify register/rhythm/entry changes and integrity issues;
it cannot establish an experience of emotional engagement or human-level quality.

The 18 targeted Node environment tests and the C++ phrase-development checks
passed. The local worker was rebuilt; no VST build/installation or Cloud deploy
was performed.

## Result and limitations

Candidate completed on 2026-10-20 at 23:05:45 ART: 60 seconds, G major, 30 bars,
eight sounding tracks, 566 direct-AI notes and zero reused/transformed/other notes.
There are no unterminated notes or within-track same-channel/same-pitch overlaps.
The AI selected five argument voices, adding the oboe to the four string voices.
All 298 selected argument notes retained identical onsets, pitches, durations and
velocities through orchestration. The complete reduced score took one response.
The planner needed one technical reference retry, as in the baseline run.

The rehearsal returned `keep`, no revised owners and empty arrays for each
owner in four window objects, instead of the required `windows: []`. This
protocol mismatch was conservatively recorded as `original retained; rehearsal
unavailable or invalid`. No AI revision was applied and no voice was erased.
Do NOT report this run as having a technically accepted positive review.

Read-only observation does not establish superiority. The opening first-violin
duration mix is unchanged (38 notes); the later continuation differs. From
00:16 and again 00:32, however, the first violin has the same duration histogram
(26 quarters, two halves, four eighths), while the second violin has 28 quarters
and two halves in both spans. Equal histograms do not establish identical rhythm
or poor music, but this is counterevidence against declaring the constancy issue
solved. These data are observations, not inputs to a rejection/rewriting rule.

Audition 00:16 for the continuation, 00:40 for the oboe entry, and 00:48 for the
closing sentence. The inherited title is identical to the baseline; use exact
job links rather than identifying the version by its title.

- Candidate: `http://localhost:4209/?job=b1a0271b-1246-47f6-bb4e-aa53e24a7200&argument=1`.
- Baseline: `http://localhost:4209/?job=349e82c8-083d-41f0-987c-6c524a1cd946&argument=1`.

Both selected-reduction and full-MIDI HTTP downloads returned 200. Browser/audio
listening was not available to the agent. The reference is preserved, production
defaults remain unchanged, and no commit or deployment was performed. This is an
audition candidate, not a validated new anchor or an assertion of emotional depth.
