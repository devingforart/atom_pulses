# Static tonal identity: authored anchors, not automatic chord following

The last 32-bar trial correctly produced three rhythmic owners but each had
only two attacks per bar and its pitches followed chord changes. This differs
from the user's example, whose constant pitch carries articulated sixteenth
groups and recurring gaps. Two attacks per bar are not inherently wrong; the
failure is treating a request for rhythmic body as occasional chord markers.

New explicitly allocated static layers include an AI-authored static_identity:
pitch_policy, anchor_pitches, harmonic_fit, rhythmic_sentence and variation_intent.
The model chooses retain_anchor or harmonic_variant against the complete harmonic
map. A fixed pitch may be an intentional extension/pedal, not necessarily every
chord's root. No rule chooses its pitches, density, chord rhythm or attack count.

The existing writer receives this declaration with the actual reference notes.
For retain_anchor, the direct-note schema encodes exactly the AI-selected pitch
set and the parser rejects a changed pitch with recovery feedback; neither
retunes a note. Harmonic_variant retains pitch freedom under the tonal policy.
Repeated-cell playback remains explicit authoring, not automatic filling.
The older unconditional instruction to adapt every lane's pitches to every
chord has been replaced with role-specific treatment. The model must express
the complete rhythmic sentence, including short attack groups and deliberate
gaps, without making every counterpart busy. No note-count floor is imposed.

Tests cover anchor preservation, intentional harmonic variants, unchanged rhythm
and gates in schema projection, exact repeated-note parsing, invalid anchors,
source nonmutation and historical compatibility. Listening remains necessary:
these contracts do not prove that the model chooses convincing musical cells.
