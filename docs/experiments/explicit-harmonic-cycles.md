# Explicit AI harmonic cycles — local experiment

Reference diagnosis: `d5baeec0-b2e6-4cc8-a968-388d8ec15dca` declared a four-bar
progression repeated over bars 1–24, but supplied only four harmonic events.
The realized MIDI held the last harmony until the next section. This experiment
does not edit that composition or reuse its seed, plan or MIDI.

New section contract:

- `harmonic_cycle_bars`: AI-selected cycle length; 0 means no repeat sign.
- `harmonic_cycle_repetitions`: number of cycles in this section, not a global bar.
- `harmonic_cycle_until_bar`: derived exclusive local end, retained in audit/export.
- `harmonic_events`: first cycle plus any explicit events after that end.

The contract resolver expands only harmonic **event addresses**, before cast,
phrases and MIDI writing. It does not create, duplicate, move or retune MIDI
notes. Every writing window receives the resolved harmonic map, with the
existing chord/tonal review operating against those arrivals. Repeating a chord
schedule does not imply identical MIDI voicings, articulation or expression.
The 32–64 layer-evolution horizon remains independent of this shorter cycle.

Conflicting explicit events or invalid boundaries get one AI macro clarification
before cast writing; they are never guessed or repaired with generated notes.
Older plans without cycle metadata retain their original interpretation.
Resolved checkpoints can be parsed again without double expansion. Explicit
codas after the repeat range remain untouched. Exported plans retain both the
cycle declaration and the resolved arrivals for audit. Expanded harmonic maps
are not truncated by the legacy 64-event sanitizer.

Verification: ClubNucleusTests covers repeat mapping, idempotence, invalid
boundaries, conflicts, legacy preservation, explicit endings and unchanged
note material. This proves scheduling integrity, not musical quality: the new
work still needs musical inspection and human listening. Natural-language
claims alone cannot be proved equivalent to arbitrary structured plans.

The first experiment failed because the AI used global end bars 64/96 for
32-bar sections. The generation schema now asks for a repetition COUNT instead
of an end position (4-bar cycle x 8 repeats = 32 bars in any section). Old
accepted until-bar plans remain readable; new plans need no coordinate guess.
# Follow-up: disjoint schedule modes

Job `748a655a-6f81-4b8b-88d1-028fbf87d42e` failed before note writing:
16-bar closing section, cycle length 4, repeat count 0. The previous numeric
fields allowed incompatible combinations. Also, their detailed guidance was
inside a disabled historical monolithic prompt, not the active macro request.

New requests use a nested `harmonic_schedule` union: `explicit` contains only
its mode; `repeat` requires positive integer `cycle_bars` and `repeat_count`.
The count includes the first performance. Active macro prompts now receive
`harmonic_cycle::planningBrief()` for all behaviors. Explicit timelines are
position-validated but not expanded. Repeated timelines compile to the existing
internal fields. Legacy plans still parse; new contracts do not guess what a
zero repeat means. Tests cover explicit preservation, repeated coda, idempotence,
zero counts, overflowing cycles and invalid explicit positions.
