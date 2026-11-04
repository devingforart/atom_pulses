# Concrete opening and response sentences — local only

Baseline: Retained Dorian Cells, job 90bdce3c-b329-4283-aff5-217ffbecdcb0.
The route improved explicit continuity but the plan still assigned a few pitches
to four-bar spans. The added eighth-note sequence had 500 notes while the lead
and counterline had 36 and 10. These are observations, not musical thresholds.

The prior reference schema capped the musical sketch at 16 beats and 24 notes.
New references allow up to 32 beats and 48 notes, without increasing minimums.
Each arc also supplies an AI-authored response_reference or null for deliberately
minimal/open/sustained intentions. Each route span names its material_source.
Both references reach the existing writer and reviewer through the arc JSON.
Their notes are examples of the intended musical argument; no renderer copies,
stretches, transposes, schedules, fills or generates them procedurally.

Guidance now requests natural sentence timing, actual answering material and
consistent MIDI octave numbering, rather than treating a motif as a whole long
arc. No extra sequencer, instrument, note density, variation quota or periodic
change is mandated. No additional API stage is added. Reference bounds and
source availability are validated; old checkpoints retain their prior semantics.

Tests exercise long references, invalid releases, nullable responses, source
selection, context propagation and compatibility. A fresh three-minute trial
uses the same prompt, seed and settings. Musical success remains unproven until
MIDI inspection and user listening; do not promote this experiment to Cloud.
