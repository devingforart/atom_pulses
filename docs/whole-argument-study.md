# Whole musical argument before orchestration — local experiment

## Hypothesis

Earlier complete-dialogue experiments actually composed the full pitched cast in
successive bounded requests. A good theme and tonal correctness did not make the
whole melodic argument perceptibly connected to the listener. This experiment
changes the unit and order of composition, not just narrative labels.

The phrase planner selects `argument_owner_ids` from actual cast IDs: the main
speaker, substantial answering/contrapuntal voices where needed and structural
bass. Six owners is a response payload bound, not a requested density or a limit
on the completed orchestration. Unknown/duplicate/unpitched IDs, omission of the
declared protagonist and planned speakers outside that selection are technical
contract failures; intentional musical rests and repetition are not failures.

One `pulso_complete_dialogue` request contains **every section of the whole work**
and writes the complete selected-voice score. Section/window objects are only
coordinate containers; there is no serial 24-bar composing boundary. The output
budget is 40k tokens and the request ceiling 360 seconds. Bounds do not force a
minimum number of notes, new themes, cadences or asymmetrical sections.

The existing bounded thematic rehearsal then hears the actual complete reduced
score numerically and can keep it or author an explicitly owner-scoped revision.
Both draft and selected reduction are checkpointed. Only after this does the
existing accompaniment writer compose the remaining owners against the complete
accepted foreground. The completed foreground is immutable in this stage.

No code copies reference notes into the piece or generates missing connections,
changes rhythms, adds rests, adjusts velocities, removes overlaps, thins, stretches
or humanizes notes. AI notes and exact scoped transactions remain the musical
authority. A reviewer saying `keep` is advisory, not proof of narrative quality.

## Isolation

`node web/local-studio/start-whole-argument.mjs`, port 4206.
Explicit `PULSO_AI_WHOLE_ARGUMENT_EXPERIMENT=1` is stripped in ordinary worker
environments. Joint-all-pitched writing and the previous consequential experiment
are off here. The accepted anchor and Cloud/VST defaults are untouched. Tests
verify the single whole-work request layout, exact AI owner selection, owner
schema and invalid-ID handling, plus ordinary profile isolation.

The local UI provides `Escuchar argumento sin orquestar` / `Escuchar arreglo
completo` for these jobs. `?job=UUID&argument=1` directly opens the selected reduced
score (`partial-02.mid`). Existing initial/selected dialogue MIDI downloads remain
available. These are the real score checkpoints, not synthesized extra layers.

## First controlled run

Job `758af1f0-ea2b-4bcc-933b-db01102b3ee6`, requested 180 seconds, 120 BPM, adaptive,
AI-sovereign, seed `202610200101`, prompt `Musica como la de mozart.`
Only the accepted macro/cast blueprint from `fd43ef6d-1f82-40a0-b8c8-78c0b3592241`
(`The Restored Breath`) is reused. Its performed notes are NOT imported; phrase
planning is requested again for the new owner contract. This keeps the tonal/form
context while testing the new writing unit. The previous blueprint contains old
development metadata, but this worker does not activate that previous experiment.

Implementation uses [OpenAI structured outputs](https://developers.openai.com/api/docs/guides/structured-outputs)
for the wire contract. Technical validity is evaluated separately from artistic
quality. Listen for a live implication carried from one phrase to the next and
concluded by the end; neither exact motif matches nor a tonic cadence prove it.

## First reduced score and AI rehearsal

The planner selected four actual owners: principal violin, replying oboe, cello
and bassoon. The initial reduced score contains 444 direct-AI notes. Its first
whole response failed technical representation because a note attack was at the
end of the final window. The second complete response corrected the representation;
no note was mechanically clipped or moved into another window.

The independent bounded rehearsal diagnosed the protagonist's final D6 above
the terminal C harmony as an unresolved final utterance. It selected only the
principal violin for revision. The exact comparison of `partial-01.mid` with
`partial-02.mid` shows 188 unchanged principal notes, seven removed and six newly
authored; all 99 oboe, 47 cello and 103 bassoon notes are byte-semantically unchanged
in pitch, beat, duration and velocity. This is a general scoped AI review stage,
not code containing this composition's note or bar numbers.

This experiment changes request scope and orchestration order. It does not
remove the pre-existing macro harmonic plan, and the response still uses section-
local coordinate containers inside the one request. Three-minute operation does
not establish capacity or quality for ten-to-fifteen-minute works. These are
explicit limitations, not grounds to claim global narrative has been solved.

## Completed listening candidate

Finished at 21:54:21 ART on 2026-10-20. Same title `The Restored Breath`, because
the accepted macro blueprint was retained for comparison; the new performed
score is job `758af1f0-ea2b-4bcc-933b-db01102b3ee6`, NOT the earlier job.

- Selected reduction: four populated voices, 443 direct-AI notes, 176 seconds.
- Full arrangement: seven populated voices, 821 direct-AI notes; all other
  provenance counts zero.
- Every selected note's owner, pitch, beat, duration and velocity is preserved
  exactly through orchestration. `tools/analyze-whole-argument.mjs` checks this
  correspondence independently of the narrative labels.
- No unterminated notes or within-track same-pitch overlaps in either MIDI.
- Last attack at absolute beat 348, last release at 352 (2:56). The AI-selected
  terminal violin C6 is preceded by a C/G/E cadential figure, unlike the draft's
  unresolved terminal D6. Final four seconds belong to that completing gesture.
- HTTP 200 verified for the reduced audition URL and both real MIDI files.
  Browser audio quality still requires human listening; no claim of actual
  acoustic listening or artistic superiority is made by these integrity checks.

Suggested listening: first the reduction, especially the oboe-led middle at
0:56–1:44, violin return from 1:44 and closing 2:40–2:56; then toggle the full
arrangement. The middle has a genuine foreground handoff, but that alone does not
prove a connected musical argument. The listener's judgment determines whether
this writing-unit change is worth retaining. No production promotion, deployment
or commit was performed.
