# Connected melodic arguments — local study

## Problem observed

The listener rejected `7aa1959a-0404-4459-9a12-a0ebf886f6b2` despite its
recognizable motif and valid ending. In the actual MIDI the violin has
84 notes, median sounding duration 250 ms, with 47 notes below 300 ms;
the clarinet has 37 notes, median 250 ms, with 26 below 300 ms. The
descriptive audit finds 24 separated short links in the violin and eight
in the clarinet. These numbers are not universal limits: short notes can
be expressive, and legitimate breaths also appear in such measurements.
The failure is a listener's judgment about this realization, not a score
that proves short notes are wrong.

## Implementation

`AiMelodicDiscourse` adds an optional AI-authored `delivery` object to each
phrase: proposition, continuation, arrival, connection, dialogue and a
selective sketch of structural tones. Each sketched tone is itself an AI
choice of pitch, phrase-local onset, duration and musical purpose. There
is no stock melodic pattern, density target or minimum note length.

The note-writing AI receives the sketch translated into its own window
coordinates, durations in seconds at the real tempo, and actual opening
and recent MIDI for the melodic speakers. It authors the complete notes,
rests, articulation and answering lines. It may refine its own sketch in
response to the realized ensemble. The renderer never fills the sketch,
lengthens notes, deletes attacks or quantizes an imagined performance.
Representation validation checks finite pitches/times, existing register
latitude and valid phrase bounds; old phrase plans remain readable. The
writer also receives the computed semitone intervals between the AI's
sketched pitches, so an erroneous interval label in prose is not its only
evidence of what the plan actually says.

The macro architect receives the same discourse priority before choosing
the global motif and dramatic development. Local provisional arrivals are
allowed while the global harmonic question remains open. This avoids
inheriting a macro plan whose sole premise is an unfinished tiny cell.

Writing is grouped by the AI-authored phrase roles: speakers/respondents
are composed together, with a chordal owner and a bass owner where present.
The remaining owners are composed against that accepted MIDI in a second
group. If the dialogue already requires every owner there is only one
group. This changes the request's scope, never generates note patterns,
and has no hard-coded violin, clarinet, genre or melodic rhythm.

The experimental writer may sustain a melodic common tone across a chord
change when every traversed chord supports it. It no longer tells the AI
that a chord-label boundary requires an attack. Technical window limits
and existing MIDI safety checks remain unchanged. This study does not
enable global rubato, add a new musical rejection gate or retune pitches.

The read-only `melodic-delivery-audit.mjs` reports real note durations,
monophonic articulation ratios, listening examples of detached links and
rapid attack groups, and the realized AI bearing tones. Polyphonic chord
attacks are not counted as a succession of monophonic attacks. Its time
conversion follows the MIDI tempo map. None of these measures establishes
emotional expression or replaces human listening.

## Local use

```powershell
node web/local-studio/start-melodic-discourse.mjs
```

Default URL: `http://localhost:4193`. The startup enables the explicit
local experimental profile, expressive ensemble and discourse-first writing.
It uses the existing private environment credential;
it contains no key. Following acceptance, the standard local and Cloud
profile also enables this same discourse architecture.

```powershell
node tools/analyze-melodic-delivery.mjs JOB_ID --output build/melodic-delivery.json
```

Compare complete one-minute realizations using the same prompt, tempo,
seed and macro blueprint, but a new delivery plan. These are stochastic
new realizations, not identical backing tracks or a note-for-note causal
test. Preserve the originals. Listen for an intelligible assertion,
continuous development, an answer that changes the argument, earned
intensity and a conclusive release. Slower notes alone are not success.

## API representation

The extension uses the existing strict JSON Schema path, with required
fields and `additionalProperties: false`. This follows [OpenAI's structured
output documentation](https://developers.openai.com/api/docs/guides/structured-outputs).
Schema validity establishes representation consistency, not musical quality.
The richer plan has bounded output headroom proportional to section count
(11,300 maximum output tokens for four sections, capped at 20,000) and a
180-second per-call time ceiling. These are transport/representation limits,
not targets for consumption or musical quotas. Delivery prose is requested
concisely; the old non-experimental budget is preserved.

## Listening candidates and observed results

`65210fdd-2448-4001-a4ef-5d170347aed8` reused the old macro blueprint with
a new delivery plan and all-owner writing. It improved sounding duration
but retained the overly fragmentary withheld-turn premise. It was not
selected as a sufficient solution to the listener's complaint.

`c7e9f469-cde9-4641-a26a-b2ea0ebe2590`, **Bearing Question, Changed Ground**,
uses a fresh macro plan and discourse-first writing. Prompt remains
`musica como bela bartok`, 60 seconds, 120 BPM, seed
`4203976088674690812`. All 336 notes in eight tracks are direct AI output,
with no procedural/reused/transformed notes and no unterminated notes.

| Observed melodic evidence | Rejected source | New candidate |
| --- | ---: | ---: |
| Violin median sounding duration | 250 ms | 500 ms |
| Violin separated short links | 24 | 2 |
| Clarinet median sounding duration | 250 ms | 625 ms |
| Clarinet separated short links | 8 | 0 |
| Median articulation ratio, violin / clarinet | .6667 / .6667 | 1 / 1 |

The new violin has 69 notes and the clarinet 31. Brief notes and rapid
groups remain available: the aim was linked delivery, not uniformly slow
writing. These comparisons are descriptive and do not prove causality,
since the new macro and note realization differ as well.

Actual MIDI listening landmarks:

- 0:00-0:16: violin D-E-G-F assertion, a weighted G and a connected
  continuation with a local resting point, ending toward the dominant.
- 0:16-0:24: clarinet takes the earlier G summit as the starting point of
  G-F-E-D, then exposes B. The violin's 0:24-0:30 return expands through
  the dominant and a descending flat-II arpeggio into D.
- 0:30-0:46: violin develops the initial contour while clarinet recalls
  its descending counterargument underneath; the upper dominant C-sharp
  is sustained before the return rather than surrounded by clipped attacks.
- 0:46-1:00: violin lands on D, yields to a clarinet descent, then recalls
  its earlier gesture. The final C-sharp/E/C-sharp/A to D is an authored
  dominant-to-tonic consequence. Final note-off is at 59.5 seconds.

The generic audit still flags chromatic/vertical tensions (36 generic
conflicts, zero low-register conflicts, six unsupported-chromatic labels).
These must be interpreted in their musical context and heard; numeric
readiness is not proof of stylistic success or human-level composition.
Use the direct job URL in the Auto tab:
`http://localhost:4193/?job=c7e9f469-cde9-4641-a26a-b2ea0ebe2590`.

An independent seed `4203976088674690913` produced
`b01c4cd7-2bc5-4d79-8ab5-182ab1200b4b`, **E-flat Falls to D**. It completed
without editorial recovery: 366 direct AI notes, eight tracks, exactly
60 seconds, no unterminated notes and one second of terminal space. The
main violin has 56 notes (median 500 ms, median articulation ratio 1,
three separated short links). Its memory violin has 40 notes (median one
second, one separated short link), and the viola's 66 notes have a 750 ms
median and two separated links. The clarinet has 12 notes, all at least
300 ms, in its particular answering role. This is not proof every voice
is sufficiently developed for every user's brief.

The generic narrative audit warns about weak thematic memory and soundscape
coverage in this second candidate. Keep the warnings. The actual main
violin opening and ending both begin D5-F5-G5-F5-C5 at relative beats
0, .5, 1.5, 3, 3.5; the continuation and C duration change. Its climax
recalls that contour with compressed G. The final authored C-sharp5-to-D5
lasts 3.5 and six beats respectively, rather than a tonic tag at the file
boundary. These MIDI facts are evidence of lineage, but a repeated motif
and smooth articulation alone do not certify a convincing narrative.

Both candidates used the discourse-first architecture. The final local
binary additionally removes an unnecessary named-artist example from the
editorial instruction, requests more concise delivery prose and raises
only the representation budget described above. These housekeeping changes
were built and covered by pipeline/unit checks; the recorded job hashes
identify the precise binaries used for the paid listening experiments.

The local status banner identifies the active profile as
**Laboratorio local de fraseo y diálogo conectado**. Standard Cloud and
VST profiles were untouched during the experiment. After accepting the
five-minute candidate, the user requested promotion to Cloud. The accepted
flags now live in the shared composer profile, so ordinary local and Cloud
requests use the same discourse architecture. See
`docs/ACCEPTED_COMPOSITION_ANCHOR.md` for the frozen reference and limits.
