# Local experiment: complete phrases and relationships between voices

## Scope

User-requested prompt: `Musica como la de mozart.`; 180 seconds at 120 BPM.
This changes the experimental generator, not an exported piece. No commit,
production default, VST build or Cloud deployment is requested or implied.
The previous prosody binary and its listening works remain intact.

The rejected five-minute Bartok-style sample `0ea15887-1f6f-4640-a97c-b4d5053f2e15`
had 918 direct-AI notes. Every onset was on an integer quarter-note beat;
148/166 protagonist notes lasted at least two beats. Viola/cello/horn used only
four- or eight-beat durations. Exact seven-event opening statements occurred at
0, 10 and 156 seconds. These facts support investigation, not a prohibition on
repetition, sustained music, narrow themes or a compulsory density target.

## Generator architecture

Opt-in `PULSO_AI_PHRASE_DEVELOPMENT_EXPERIMENT`:

- Keep a primary reference sentence, but allow the AI to author up to four
  additional complete reference materials with their own notes, continuations,
  arrivals and relationships. An empty array is legitimate for monothematic music.
  This is a bounded transport representation, not a requirement for five themes.
- Each phrase identifies its material, inherited relationship, musical task,
  partner's task and actual intended consequence. A section no longer implies
  only one or two phrase intentions; the existing schema remains bounded.
- The experimental cast policy removes the maximum-one-answerer instruction.
  The primary ID remains for tracking, not an exclusive right to invent material.
- All pitched owners are still composed jointly. The old subject-only realization
  instruction is replaced in this variant, rather than merely contradicted by an
  appended request for extra notes. The AI chooses every real note and rest.
- Optional actual-score AI rehearsal receives per-section onset subdivisions,
  duration histograms and register ranges. These are evidence, never acceptance
  thresholds, automatic rhythm edits or mandatory novelty.

Reference materials NEVER create, copy, transpose, invert, time-stretch, distribute
or fill MIDI. Only the later AI-written performance becomes audible. Validation
checks representation, note coordinates, known material/owner IDs and bounds; it
does not enforce a musical score or density quota.

## Reproduction

Build only the isolated worker/tests in `build-development`, with the existing
JUCE source cache, plugin and CLI disabled. Launch:

```powershell
node web/local-studio/start-phrase-development.mjs --rehearsal
```

The local server defaults to port 4203. Without `--rehearsal`, the new generator
still works but retains its directly authored first draft. The shared Cloud
environment never forwards this experimental flag.

Control A uses the preceding prosody/rehearsal worker at 4202:
`60b7a44f-886c-46e7-b74f-d9c19b62facd`.
Experimental B uses the development/rehearsal worker at 4203:
`7538b78d-e0ed-497d-8b7b-8dbbc7c37fae`.
Both trials use the exact prompt above, seed `4203976088674691133`, duration,
tempo, adaptive behavior and Terra/medium. Neither imports notes or an older plan.
Their macro/cast/planning can differ: a seed does not make API generations
deterministic, so this is an exploratory comparison, not proof of causality.

### Restart recovery

A machine reboot interrupted both jobs before their first complete MIDI checkpoint.
Control A had an accepted blueprint and a completed phrase-planning API response.
The exact phrase JSON was retrieved from that existing response (no new generation)
and saved as its `phrase-architecture.json`. Restart A
`edc20bce-684e-4e7b-80c9-8c82e3a86e34` reuses that accepted macro/cast/phrase plan;
its performance is newly AI-authored, not claimed as the lost original notes.
Experimental B had not persisted a full accepted blueprint, so restart B
`6e8fa42e-e2c5-434d-8a10-59cee7563f2e` is a fresh full request with the same inputs.
Neither interrupted run is treated as a completed or successful musical sample.

Restart B stopped before MIDI: its first plan assigned an answer outside the
owner's active sections, and its second added an `unused` material with an empty
owner. These are representation/cast errors, not rejected musical notes. The
development schema now binds reference and phrase owner fields to the actual
AI-selected cast with JSON-schema enums; an empty response remains a legitimate
rest, but an unowned material is not a real reference. The prompt explicitly
omits unused entries instead of filling the array. No dummy item was silently
removed, mapped to another owner or turned into MIDI. The next retry reuses B's
accepted macro/cast and requests a new AI phrase plan, rather than paying for
another orchestration blueprint.

Next attempt `0eb9599a-e7d2-4a1e-813b-33d1c0c9ee1c` exposed a local contract bug:
`inherited_from` contained a meaningful explanation such as `primary: the delayed
descent...`, but validation incorrectly required that entire description to equal
an exact material ID. `material_id` remains an exact reference; `inherited_from`
is now explicitly bounded free musical text in both schema and validation. A
regression test covers that distinction. The first response is preserved as
`rejected-phrase-plan-01.json`; recovery can revalidate those exact AI intentions
without asking the model to replace their content or generating MIDI from them.

## Whole-score rehearsal transaction correction

Generation `81d01845-01c5-4cea-8a0f-3fdaa6c0284b` successfully wrote a 1193-note,
eight-part, 176-second first score, saved in `partial-01.mid`. Its rehearsal
diagnosed one terminal viola note, returned that complete part and empty arrays
for the other parts. The previous whole-score interpretation incorrectly accepted
169 viola-only notes as the revised composition. The actual MIDI audit detected
the loss; this is a discarded candidate, not a successful musical comparison.

The rehearsal protocol now requires explicit `revised_owner_ids`. A listed owner
supplies its complete replacement notes across every requested window; unlisted
owners supply empty arrays meaning UNCHANGED. The application rejects notes outside
that declared scope and preserves all unselected original notes and all existing
controls. A listed owner may intentionally rest; there is no note quota. Scope
must never be inferred from prose diagnosis. The complete first draft is retained
if representation or technical validation fails. Owner selection, pitches,
durations and rests are AI decisions; this transaction does not compose them.

Regression tests cover immutable unselected notes, controls, ambiguous missing
scope, intentional selected-owner silence, and descriptive inheritance. Recovery
`9b610e27-fa42-4383-8319-a64e0be78e76` imports the exact original complete foreground
and repeats only the AI rehearsal, not composition or planning. Exact MIDI comparison
confirmed 1193 notes in all eight parts and unchanged note attributes at import.

## Completed exploratory comparison

Control A: **The Answer After the Pause**, job
`edc20bce-684e-4e7b-80c9-8c82e3a86e34`, 176 seconds, eight populated parts,
777 direct-AI notes. Its primary violin has 218 notes and second violin 89.

Experimental B: **The Withheld Descent**, selected job
`9b610e27-fa42-4383-8319-a64e0be78e76`, 176 seconds, eight populated parts,
1226 direct-AI notes. Its primary violin has 227 notes and the independent second
violin 162. This is not an argument that more notes means better music: much of
the increased total is chordal piano/body writing, and the user changed style
from the earlier Bartok work. A and B here share Mozart prompt, seed and tempo
but not a fixed blueprint; preference is not causal proof.

B's actual primary statement E4-F4-G4... is heard at 0 s and 24 s, while the
second violin enters around 14 s with a lower descending A-G-F-E-D-F-D-C argument.
The counterargument has its own substantial role at 56-88 s. The scoped AI
rehearsal selected only `vln1_protagonist`, rewriting beats 176-224 (88-112 s):
181 complete primary notes were retained, 13 removed and 46 authored by AI,
supplying a continuation where that voice had dropped out. Exact comparison
confirms every note of the other seven parts is unchanged. Eight parts remain;
no notes are marked procedural, no hanging notes were found. Audits are stored in
the job directory, including `foreground-revision-diff.json`.

Reservations for listening: the cadence contains a sustained Cmaj7 sonority,
with an added D in an inner line at the very end, so root arrival alone is not
proof of a stylistically convincing close. A same-key horn overlap across
bars 76-77 remains unchanged; it is advisory evidence, not automatically clipped.
The new revision has substantially more foreground activity at 88-112 s and may
or may not be preferable to the original fragmented passage. These are candidate
listening judgments, not an automatic promotion or a claim of human-level music.

Listening URL: `http://localhost:4203/?job=9b610e27-fa42-4383-8319-a64e0be78e76`.
Reference URL: `http://localhost:4202/?job=edc20bce-684e-4e7b-80c9-8c82e3a86e34`.

Only this experiment gives the expanded phrase representation extra output
headroom (up to 24k tokens). Its optional whole-score rehearsal output capacity
scales with the actual draft size, capped at 48k, and has a 360-second maximum
instead of the previous 180 seconds. These are transport ceilings, not creative
quotas, required expenditure or extra retries. First drafts are preserved even
when review is unavailable. These ceilings do not activate in the accepted path.

`tools/analyze-phrase-development.mjs` reports the actual MIDI and narrow exact
rhythmic reference matches. It does not equate more notes, more fractional onsets,
more themes or a model's self-approval with better music. Inspect the actual score
and obtain listener feedback before claiming the experiment succeeds.

Implementation follows [structured-output representation guidance](https://developers.openai.com/api/docs/guides/structured-outputs)
and separates technical checks from artistic evaluation as described in
[evaluation best practices](https://developers.openai.com/api/docs/guides/evaluation-best-practices).
