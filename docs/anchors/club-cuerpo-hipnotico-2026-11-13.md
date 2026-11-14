# Listener-approved Club groove anchor

Local Git reference: `motor-club-cuerpo-hipnotico-2026-11-13`.
Branch at capture: `v1.2`. No push, Cloud deployment or branch promotion.

The user accepted the concept as better than preceding candidates and asked to
preserve this exact engine before continuing development. This is an approved
development anchor, not a claim that musical quality is finished.

## Audition and provenance

- Title: Pulso de Re — Cuerpo Hipnótico.
- Job: `35cd4144-e138-44d6-86f9-30793e5b4408`.
- Local audition: http://localhost:4314/?job=35cd4144-e138-44d6-86f9-30793e5b4408
- Seed: `9133951924945030018`.
- Duration: 64 seconds / 32 bars / 120 BPM / 4/4; D major.
- Model: `gpt-5.6-terra`, medium reasoning.
- Mode: Club; layer pace auto; resource variety on; pitched-only; chromatic off;
  AI sovereign; first-pass audition on; phrase architecture on.
- Accepted macro/cast reused from `a4e5efc1-4824-44d6-8fe4-9306cde433e8`.
- Worker SHA256: `2304db8050ea8acc701c10da97729b1d9e85ab6009870d6cb42e674678951e63`.
- Full MIDI SHA256: `496b75bf39b714f24e676827081437e361cd718c8da3534a921c2525a266a0f9`.

Prompt:

> Seccion completa de produccion club: hipnotica, intensa, repetitiva y dinamica. 32 compases, 4/4. Seis pistas: bajo; stabs de acordes cortos y repetitivos; dos capas ritmicas tonales complementarias de altura estable; colchon sostenido; hook breve. El groove y los stabs llevan la identidad, no una conversacion de solos. Celulas articuladas con grupos de semicorcheas, acentos y huecos reconocibles; conservarlas durante varios ciclos. Armonia estable cuando sirva; evolucion por capas y tension. Sin baterias ni percusiones. Debe tener cuerpo incluso sin el hook.

## Captured engine behavior

Includes groove-led Club short scores, explicit functional allocation, actual
polyphonic chord stabs, static pitch identities, joint rhythmic hierarchy and
AI-declared shared attack spaces. Includes exact monophonic-onset serialization,
explicit reference endpoint clocks, section-keyed energy summaries and bounded
AI reference/declaration reconciliation. MIDI export event checks remain active.
No new host-authored note patterns, automatic gap filling or pitch snapping.

Six tracks, 1152 notes; direct AI 936, explicitly reused AI 216. Export verified
for full MIDI and six tracks. No off-grid attacks, same-pitch overlaps or audited
sustained semitone/minor-ninth collisions were found in this final MIDI.

Known musical concern: bars 9-10 still occupy 28/32 sixteenth-grid positions.
Stabs have a more distinct accent hierarchy, but bass phrasing may still compete
with their gesture. Preserve this candidate while testing further improvements;
passing consistency tests is not evidence of finished production quality.

## Restore and reproduction

Inspect the anchor with `git show motor-club-cuerpo-hipnotico-2026-11-13`.
Switching revisions requires protecting unrelated uncommitted work first.
Source changes after this anchor should not overwrite the tag.

Runtime source data remains in:
`build/arrangement-development-studio-data/jobs/35cd4144-e138-44d6-86f9-30793e5b4408/`.
An independent local artifact copy and the exact worker are retained under:
`build/anchors/club-cuerpo-hipnotico-2026-11-13/` (ignored build artifacts, not Git).
Keep that copy when cleaning local generation history.

Start local development with `scripts/start-arrangement-development.ps1`.
It uses the tracked `web/shared/composer-profile.json` plus
`PULSO_LOCAL_EXPERIMENTAL=0` and `PULSO_AI_CLUB_ENSEMBLE_SCENES=1`.
Credentials come from private configuration, never this document or Git.
The same seed/model/prompt does not guarantee byte-identical future AI output.

Verification at capture: `pulso_club_nucleus_tests.exe` passed. The installed
worker hash matched the generation's recorded worker hash. No new generation
or musical change was made while saving this reference.
