# Joint phrase landmarks — local candidate

## Motivation

Circuito Dórico (8defaa48-a2b8-4a06-90cf-34b8280696ee) had a continuous
harmonic bed and a developed ending, but the foreground remained fragmentary.
Its F5 repeatedly sounded over E5 in the sustained support. Scale membership
alone does not establish an intentional, resolved vertical relationship.
The counterline repeated a short ornament instead of developing a response.

## General motor change

Under the existing local club closure experiment flag, each melodic statement
now includes `ensemble_parts`: AI-authored support voicings and answering
sentences at selected phrase landmarks, with actual owner, pitch, onset,
duration and velocity. All coordinates are arc-local. The writer receives
window-relative coordinates, including negative onsets for already held notes.

The planner composes these simultaneously with foreground material. The writer
is instructed to replace a reference voicing at those moments rather than
stack both; intermediate accompaniment remains connected. Landmarks are not
a complete score, a mandated density increase, or compulsory variation.

Validation checks only owner, numeric/grid validity, register, tonal policy
and arc bounds. Legacy plans without the field remain readable. No renderer
copies these planned notes into the MIDI; the AI authors the performance.
There is no new musical rejection threshold or automatic pitch repair.

## Verification and listening protocol

Tests cover schema, instructions at both stages, read-only validation,
invalid owner/span, cross-window held notes, and exclusion after release.
Build the worker and nucleus tests only; do not rebuild/install the VST.

Generate from scratch using exactly `musica como la de hernan cattaneo`,
180 seconds, 122 BPM, Club, chromatic disabled, first pass enabled.
No blueprint, checkpoint, previous MIDI or fixed seed is supplied. This is
a fresh-decision listening comparison, not a controlled same-seed A/B;
one result cannot establish a general musical improvement.

Cloud and committed production remain unchanged. Musical effectiveness is
unproven until the resulting score and human listening are evaluated.
