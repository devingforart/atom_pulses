# Club: groove-led form and actual rhythmic chords

The preceding trials were technically valid yet still treated the composition
as a changing harmonic/melodic argument with rhythmic decoration. The macro
schema required four palette chords, three sections and two harmonic events per
section (except some very short cases). These were minima, not proof that every
bar had to change, but they made a one-harmony production body unnecessarily
hard to express. Club now permits one palette chord, one section/act and one
harmonic event. Non-Club schema behavior is unchanged. The AI chooses whether
to use a stable state or a progression; the host does not remove its events.

For preserved authored Club plans, normalization now retains a nonempty one-chord
palette rather than replacing it with the local home/motion fallback. Empty
palettes and other modes keep their existing fallback. A regression checks the
authored palette and sole event after actual normalization.

Club uses a dedicated groove short-score brief: the whole bass/chord/pitched
rhythm interaction may carry the argument. Melodic development is available,
not compulsory for every layer. The joint excerpt can contain up to 128 notes
instead of 32 total across all owners; chord tones count individually. The
bound is representation capacity, not a note quota or automatic density target.

Requested function rhythmic_chords is distinct from a single-tone pulse or held
pad. Its schema declares a static rhythmic identity with attack_structure=chord
and polyphonic=true. The direct MIDI parser checks distinct simultaneous pitches
at each authored chord attack; it never adds a harmony voice. Empty arrays still
mean withdrawal. Legacy identities without this declaration retain compatibility.

The new audition asks for one full 32-bar Club production section: bass, repeated
chord stabs, two fixed-height rhythmic layers, a held pad and a short hook. This
is a groove/body test, not certification of a full-song intro/development/outro.
All note patterns, voicings, harmony and withdrawals remain AI-authored.
