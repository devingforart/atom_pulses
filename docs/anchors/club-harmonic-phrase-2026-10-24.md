# Club harmonic-phrase listening anchor — 2026-10-24

The user accepted the **Sombra de Do — A/B frase armónica 9–12** local
audition as the current Club listening reference. It is an experimental
AI-authored revision of one completed three-minute first pass, **not** a
claim that the shared generation engine now performs this review for every
prompt or that Cloud/VST have changed.

- Accepted local job: `663cc430-f245-4c58-b49b-dcfa143273e5`.
- Source local job: `1e8675dd-3ed5-49ef-b125-3b3b02277fd0`.
- Request: 180 seconds, 122 BPM, Club, C minor, seed
  `6484725674144142049`, chromatic notes disabled, pitched-only.
- Source flaw: the primary sustained chord track stacked 11 notes at bar 9
  for four beats, then had no notes in bars 10–12. A chord-only A/B changed
  the voicing but left those empty bars untouched.
- Accepted edit: the AI rewrote **only** that sustained track in bars 9–12
  (beats 32–48), connecting Cm9, Fm7, Abmaj7 and Gm7 with 11 authored
  notes across the four bars. No MIDI note was generated procedurally.
- Validation: no silent subdivisions in the target harmonic body, no
  hanging notes, no non-chord pitches, and all other notes and tracks
  byte-preserved by the focused MIDI replacement.
- Frozen revised MIDI: `experiments/anchors/05-club-harmonic-phrase-3min/full-song.mid`.
  SHA-256 `5d310f484d7413ff8e9f83fac49252cd848cd55e33437c5579be47729c558581`.
- Frozen source MIDI: `experiments/anchors/05-club-harmonic-phrase-3min/reference-standard.mid`.
  SHA-256 `54954cdf32d2d54ee22724a47323250720697ca3159d2dc7774d0f56e2ce13d9`.
- The AI response and exact before/after note ledger are preserved in
  `experiments/anchors/05-club-harmonic-phrase-3min/harmonic-phrase-report.json`.
- A/B listening comparison: `1fe6ea86-79f8-456d-be40-5bd6ab913cfa`.

The user's high-confidence evaluation was 4/5 in all nine criteria,
`would_use=yes`, with no listed problems. Their observation was: “Por
ahora va bien. Dejemos este motor actual como base. por ahora viene
mejorando.” Interpret this as acceptance of the **audible candidate**.
Do not silently promote the local four-bar repair to the general engine:
that needs a separate generalized implementation and multi-prompt A/B
validation. The current code baseline remains the separately saved
`candidate-club-2026-10-24` tag at `99798fb`.
