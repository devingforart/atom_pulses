# Club rhythmic identity and remaining cohesion risks

## Implemented experiment

Previously `cycleBoundLayer` returned true for every groove core owner before
reading its explicit timing_mode. Validation also prohibited flexible core
voices. Core membership therefore conflated musical importance with invariant
attacks. The meter carrier now remains bound, while other owners can explicitly
choose phrase_flexible and develop related attacks. Absent timing metadata keeps
legacy core commitments. Existing explicit cycle_bound plans remain unchanged.
Planner and writer guidance now agree with that distinction; no generated notes
are retimed, randomized, duplicated or expanded. No new preset rhythms or
mandatory variation intervals. Stable patterns remain a valid AI choice.

This permits variations through the existing flexible grammar; it is not a new
per-section rhythm-cell system. It does not guarantee more varied output.

## Inspection findings, not all implemented changes

- Club editorial guidance strongly privileges stable short cycles and warns
  against competing syncopations. Useful for meter, but accumulated prohibitions
  can encourage overly conservative phrase designs. A simple quarter carrier is
  an AI choice influenced by this direction, not an installed rhythm template.
- Technical Club writing windows usually span eight bars. The writer prompt
  requires notes to release within each window. There is no equivalent native
  cross-window note tie in that output instruction: a musically sustained voice
  may be rearticulated or shortened. This is a concrete structural risk, not
  proof that it caused every awkward phrase. A future tie-aware representation
  should preserve authored duration without automatically extending notes.
- Short-score phrases are bounded reference excerpts, while the writer must
  develop them over longer spans. This compression can produce cue-like music
  even when the prose promises a continuous sentence. More prompt text alone
  does not establish a realized melodic argument.
- Multiple descriptions (short score, scenes, arcs, layer states) can disagree.
  Pre-MIDI AI reconciliation mitigates this, but cannot certify audible cohesion.
- First-pass audition returns before downstream musical revisions, retaining
  technical checks. Our trials use it, so a successful generation is not proof
  of having passed all later musical reviews. This behavior was not changed.

Predictability means intelligible expectation and consequence, not identical
rhythms. New seeds alone confound comparison; listen across several works before
promoting the candidate. No Cloud deployment or active-job cancellation here.

## Short-work request sizing experiment

For Club works up to 40 bars with at most three owners, request a complete
AI-authored section per writing window rather than subdividing it every eight
bars. Section boundaries and all authored music stay unchanged; large casts,
long works and non-Club request capacities remain unchanged. This reduces
technical release boundaries only where a small score fits the existing
response budget. It is not a general cross-section tie implementation and
does not guarantee improved phrasing. Tests cover short/small, large-cast,
long-form and non-Club capacities. The trial uses the user's exact prompt
`musica hipnotica y evolutiva`, 60 seconds, Club. If the AI chooses more than
three owners, the normal request sizing remains in effect.
