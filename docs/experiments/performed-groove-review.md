# Joint review of the actually performed Club gesture

The new optional musical review runs after complete Club writing (and after the
existing isolated voicing review on that route), including the current first-pass
audition route. It is not a global rewrite or a numerical syncopation gate.

Scope comes from the AI's gesture coordination: a movement-bass owner and actual
chord-attack identity owners, including the declared gesture leader. No track-name
heuristic selects them. The AI sees their actual rendered attacks plus all other
rendered notes, chord events and the declared leadership/intensity intentions.
It can keep the original, or explicitly propose individual attack times, gate
deltas and velocity deltas. No rule says a sixteenth stagger is wrong or that
the pair must attack together. No minimum reduction in offset counts is imposed.

Only direct, uniquely placed, untransformed source attacks that exactly match
rendered events are addressable. A chord attack moves as one group; original
pitches, note count and internal velocity differences stay unchanged. Applying
an AI edit does not invent offsets, fill silence, snap notes, delete notes or
clamp unsafe proposals. Unknown/duplicate addresses, out-of-cell/section edits,
unauthorized fields or invalid MIDI values reject the proposal atomically.

The candidate is rendered and checked for protected outside events, all pitch
multiplicities, controls, expressions, marker/length preservation, MIDI safety,
non-increasing same-pitch overlaps and non-increasing invalid sustains. An
additional AI verdict compares actual before/after notes and harmonic context;
acceptance requires that verdict to favor coherence without flattening identity
or intensity. This is at most one proposal and one comparison, not an unbounded
rewrite loop. The original is retained if a review is unavailable or unsafe;
failure is not silently presented as an improved groove.

`performed_groove_review` in the persisted architecture records keep/revision,
proposal, verification and outcome. Accepted final performance events are the
AI's explicit revision of the initial rhythmic reference, not a claim that those
events still duplicate the original nucleus exactly. Audit against the actual
performance and its revision evidence. Future extensions must preserve that
accepted MIDI rather than recreate it from the initial reference alone.

Audition snapshots `before-groove-review.mid` / `after-groove-review.mid` and their
plans retain exact in-run A/B evidence. Their checkpoints preserve the writer's
completed-window count. No new UI setting, deployment, host-authored pattern or
guarantee of aesthetic improvement is introduced. The listener remains decisive.

Compatibility: a generic prompt can legitimately produce a polyphonic
`harmonic_pulse` / `static_tonal_rhythm` layer without optional `static_identity`.
That explicit functional commitment also identifies a chord-pulse participant;
display names and timbre labels do not. Every Club run now records why review
is not applicable or how many owners/actual attacks were addressable, rather
than silently skipping it. `--inspect-groove-scope JOB_DIRECTORY` checks the
same selector against an archived accepted cast and phrase plan, read-only.
