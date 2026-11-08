# Club plan contract recovery (local)

Two planning failures were not handled by the focused AI recovery dispatcher:

- A layer declared duplicate/out-of-cycle rhythm offsets.
- A declared groove-core owner had no corresponding tonal-nucleus lane.

Neither failure is repaired by moving MIDI notes or dropping a voice. Recovery
asks the AI for only the affected rhythm_cell or the missing lane's notes and
repeat_intent. The host merges that field/voice and validates the complete plan
again. Existing harmony, cycle, cast and other lanes remain unchanged. Recovery
retains the existing two-attempt/time-budget ceiling; it does not guarantee
arbitrary invalid responses will become valid or claim improved musical quality.

The worker was rebuilt locally. These changes are not deployed to Cloud.

## Final-scene evaluation

Club closure now recognizes a connected exact retrograde fragment of at least
four notes from the opening five-note subject. It does not generate or retime
notes, and a disconnected final tag cannot borrow that evidence.

Final publication and narrative audit share `NarrativeScoreGate::resolved`.
For Club this requires a realized terminal phrase, honored handoff, causal
continuity, thematic evidence and stable terminal harmony. Register descent and
reduced ensemble density remain reported, but are not compulsory musical proof
of closure. Adaptive retains the previous resolution-score threshold. This fixes
the contradiction where a valid Club cadence was rejected for keeping energy.
Technical safety, voice development, harmonic continuity and comparison of
candidate versus accepted MIDI remain separate checks.

## Contextual ending adjudication

An independent AI comparison can explicitly validate the original ending while
rejecting a revision. Previously that decision preserved the original MIDI,
then the final gate rejected it again using the same heuristic that triggered
the revision. A favorable original verdict now records runtime-only evidence
and an exact signature of the reviewed note set and harmonic/phrase context.
Only that ending check can use this evidence; all other publication checks
remain. A mere preference for the original is insufficient: the reviewer must
explicitly mark its musical goal realized and supply evidence and a reason.
Changed notes/context invalidate the approval. No approval is restored from
checkpoint JSON. Final review diagnostics report contextual acceptance and
evidence separately from the unchanged numerical resolution metrics.

The comparison admission now depends only on technical MIDI integrity and
preserving earlier scenes, not on the disputed narrative score. Candidate
acceptance still requires the non-narrative quality-preservation checks and a
full final review with the exact contextual approval. A critic preference alone
cannot bypass another publication failure. Invalid or unavailable verdicts do
not create an approval. This removes the circular prerequisite that prevented
the critic from adjudicating precisely the heuristic failure being reviewed.
