# Explicit AI tonal foundation

The planner distinguishes stable_foundation and developing_voice per layer.
This classification is separate from cycle_bound versus phrase_flexible: a
stable musical identity is not necessarily a single fixed attack grammar.

Stable owners may return an ordinary explicit note array or an authored cell
with cycle_beats, repeat_count and notes. The parser keeps ONE PerformanceCell
and an explicit repeated PerformancePlacement; it never invents a rhythm,
changes pitches, adds variations or fills the unused window. Legacy arrays
remain valid. Repetition requires an AI foundation declaration and a positive,
bounded count; the cycle and all its events must fit the assigned window and
owner activity. Every repeated attack must satisfy the owner's timing grammar.
Ordinary arrays remain available for harmonic changes and phrase-end variants.

This is exact playback of AI repeat signs, not hidden generative postprocessing.
It permits static-pitch body layers without requiring a melody from each one.
It does not prove that the AI selects complementary functions or hears a groove.
Repeated notes are traced through the existing AI-reused provenance markers.
MIDI round-trip checks remain active.

Tests verify eight realized notes from a two-note cycle explicitly played four
times, unchanged pitch/gate/owner, retained compact representation, rejection
of out-of-window repeats and rejection without the foundation declaration.
The listening trial is 32 bars / 64 seconds at 120 BPM. Compare the combined
tonal foundations alone, then with the main voice. No deployment or promotion.

## Distinguish static tonal rhythm from harmonic support

The first candidate supplied a stable bass and changing chord pulse but classified
the counterpulse as a developing voice. It did not fulfill the requested layering.
New plans distinguish static_tonal_rhythm (near-fixed pitch/interval, short rhythmic
attacks) from stable_foundation (harmonic support which may follow the progression).
Both may use explicit repeat signs. Multiple requested static layers need separate
owners and overlapping purposeful activity, not one carrier plus several pads.
The new trial explicitly requests six tracks: three complementary static tonal
rhythms, bass, harmony and main voice. This count belongs to the trial, not a
hardcoded global template. Actual compliance still needs final MIDI analysis.

Groove interlock describes the relationship between distinct existing bass,
harmonic and foreground owners. A sustained harmonic bed need not also be an
invariant rhythmic core. Validation preserves owner existence, distinctness,
foreground identity and declared shared-attack checks without forcing that
unrelated classification. No notes or core memberships are rewritten.

## User MIDI reference: intensity_rythms.mid

Read-only inspection: 16 bars, 4/4, pitch 60 only, 126 quarter-beat gates,
velocities 99/100. Alternating six-attack and ten-attack bars with a final
eight-attack variant. All attacks are on the sixteenth grid. This is evidence
of the desired function, not a rhythm preset or a hardcoded pitch/template.

The writer now receives a separate read-only projection of each static rhythm
owner, its exact AI reference notes, cycle, interaction and arrangement states.
Static owners are exempted from the conflicting instruction to develop every
lane melodically. The AI still authors all cells and their repetitions. No
mandatory count or rhythm is introduced; the listening trial requests layers.
