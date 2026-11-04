# Concrete phrase material (local candidate)

Baseline: 786b8e43-c12e-4d34-aa3e-0ded8c656b5b, worker 591BAA2189402C2A78D773FAB058D67625266B22271FC0E49CE96AC9D31DF216.

Observed imbalance: the groove has a literal MIDI nucleus, while foreground arcs
only had prose. In the final score the two rhythmic support owners contained
982 notes, versus 43 lead and 16 counterline notes. Counts alone do not establish
quality, but user listening confirmed monotonous support and undeveloped voices.

New Club nucleus plans require a bounded AI-authored phrase_reference for each
musical arc (4/8/16 beats, up to 24 notes). It is an opening/continuation/arrival
reference, not a template that the renderer expands. It travels with existing
arc context to the note writer, including adjacent arcs. The AI writes every
final MIDI event. No copying, transposition, forced density, extra instrumentation
or automatic repetition is introduced. Historical plans remain readable.

The nucleus writer no longer demands all lanes retain identical accents and
gates: only the meter carrier keeps its subdivision and cycle-bound voices keep
their permitted attack grammar. Other expressive properties can develop at
planned consequences. Core timing validation is unchanged.

Validation checks reference timing, owner register and velocities. Existing
final-score tonal checks remain authoritative. Native tests cover reference
propagation, span/register errors and compatibility. These are integrity tests,
not evidence of musical success.

Trial: fresh three-minute Club etude, same prompt, seed, tempo, 16-32 preference
and review settings as baseline. No old MIDI or blueprint is supplied. Human
listening must decide whether the phrase develops, the support has meaningful
changes, and the groove and closing remain convincing. Do not promote to Cloud
or claim causal/aesthetic improvement solely because this trial completes.
