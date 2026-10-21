# Joint thematic-harmonic groundwork (local research)

## Question

Does composing a concrete thematic statement, continuation/answer and harmonic
support together **before fixing the full arrangement** produce more specific,
consequential music than fitting independently invented melodies into an already
frozen harmonic journey?

This is a hypothesis, not a demonstrated improvement. More notes, distinct themes,
technical validity or an AI claim of narrative do not establish musical quality.

## Implementation

`PULSO_AI_THEMATIC_GROUNDWORK_EXPERIMENT=1` requires the existing local development
experiment. The first macro response emits `composition_groundwork` before its
other properties: actual reference notes by symbolic musical role, a shared
local harmonic path, phrase spans/destinations and the proposed formal consequence.
The same response then derives its full-work harmony and form from that passage.
This adds no extra API request. Choosing instrument owners follows afterwards.

The accepted blueprint preserves the original passage. Phrase planning sees it,
maps roles to the actual cast, and the stored phrase architecture carries that
passage to the joint pitched-ensemble writer. Every realized note still comes
from that AI writer. No code copies, loops, transposes, fills, thins, quantizes or
humanizes the references. The local reference bounds are payload bounds, not
quotas for the number of themes, voices, notes or cadences in the full work.

Technical validation checks finite local spans, numeric MIDI values and known
symbolic role references. It does not grade musical intervals, enforce density,
ban chromaticism, require silence or rewrite notes. Invalid data stops safely.

Strict representation follows [OpenAI structured-output documentation](https://developers.openai.com/api/docs/guides/structured-outputs).
Schema adherence is not a guarantee of the accuracy or artistic value of music.

## Isolation and reproducibility

Start `node web/local-studio/start-thematic-groundwork.mjs` (port 4204).
The experimental flag is stripped by the ordinary worker environment. Existing
4202/4203 listening material and production defaults are retained. The worker is
the isolated `build-development` target, not the VST or deployed Cloud worker.
No automatic thematic rehearsal in this launch: expose the actual first score.

Fresh candidate: `0b6f1bf1-51ed-42a1-80f8-da3a9a2eaa4d`.
Prompt: `Musica como la de mozart.` Requested 180 seconds, 120 BPM, adaptive,
AI-sovereign, seed `4203976088674691133`, first-pass and phrase planning enabled.
Reference: `9b610e27-fa42-4383-8319-a64e0be78e76` (The Withheld Descent).
The earlier reference includes an AI rehearsal; therefore this is an exploratory
new-generator comparison, not a controlled estimate of one isolated prompt effect.

## Listening questions

1. Is the complete initial thought recognizable beyond a generic scalar cell?
2. Does its response change its implication, rather than just overlap its notes?
3. Do bass and accompaniment articulate that thought rather than provide a uniform
   grid of block chords beneath an unrelated stream?
4. Does the middle prepare something that the climax actually delivers?
5. Does the ending answer the established thought in sounding notes?

Read-only MIDI evidence and listener judgment are recorded separately. Do not
present a technical pass or a high exact-match count as proof of musical success.

## First complete result — 2026-10-20

`The Held G Answered`, job `0b6f1bf1-51ed-42a1-80f8-da3a9a2eaa4d`, completed
19:24:07 ART, after starting 19:17:56 (about 6m11s). No transport recovery or
post-generation musical rewrite. Terra/medium, 176 actual seconds, 88 bars,
eight populated tracks, 1,011 direct-AI notes; every other provenance count zero.

The reference contains a 32-beat passage with 44 principal notes and nine bass
notes. Read-only exact correspondence finds the entire principal passage in First
Violin and the entire bass in Cello at beat zero. The original joint passage is
preserved byte-semantically through the stored phrase architecture. Its harmony
also appears in the accepted opening section, including the beat-30 tonic arrival.
Code did not place any of these reference notes: the AI realization authored them.

Observed form and actual part behavior:

- 0:00–0:32: complete statement and answer, then continuation with shorter
  exchanges, not just a generic cell supplied over a preselected harmonic plan.
- 0:32–1:04: primary develops the turn; its last attack in this section is at
  absolute beat 87. Other voices continue the remaining section.
- 1:04–1:36: second-violin counterthought; principal recalls its own identity
  during the latter half. The bassoon remains active beneath them.
- 1:36–2:08: compressed higher-register principal and answering second violin,
  with the bass/inner motion maintaining continuity.
- 2:08–2:40: expanded answer and coordinated cadential arrival.
- 2:40–2:56: recognizable opening contour returns in lower register; final
  dominant B–D–F–D–B resolves to C at beat 344 (2:52), held to beat 352 (2:56).
  The terminal ensemble is octave/unison C, not the previous candidate's Cmaj9 color.

No unterminated notes or same-key overlaps within an individual track. Minimum
note duration is half a beat; this is an observation, NOT an enforced aesthetic
rule. Supporting strings now sustain substantially longer rather than adding a
fortepiano block-chord attack every half bar. However, bassoon has 193 notes and
can still make the middle feel busy or too sequential. Exact material retention
and a clear tonal cadence do not establish a compelling global narrative.

This is a worthwhile listening candidate, not a proven universally superior
composer. Both arrangement and macro plan changed, so the old versus new complete
work comparison is exploratory, not a single-variable musical proof. No commit,
VST installation, Cloud deployment or production-default change was performed.
