# Unified writer-view experiment

New reconciled Club plans mark `unified_execution`. Before building the MIDI
writing prompt, a read-only projection removes competing short-score and
ensemble-argument prose, prior grounded-story prose and act-link drafts. The
reconciled musical_arcs retain their literal phrase/response reference notes,
routes and consequences. The harmonic map, nucleus, layer grammar and activity
bounds remain intact. Scene identity remains available for grouping, but its
earlier free-text narrative is not delivered as a second writing authority.

The full source plan remains archived, including its prior drafts. Projection
does not change accepted MIDI or invent note events. Existing checkpoints
without the marker keep their previous behavior. This is an experiment in
context architecture, not a replacement of AI writing with procedural music,
nor proof that a single semantic plan has been achieved throughout the entire
application. In particular, inconsistent arcs/states can still survive review.

Tests cover unchanged archived sources, removed competing text, exact retained
reference notes and idempotence. The fresh listening trial uses Club, 60 seconds,
124 BPM and `musica hipnotica y evolutiva`. No deployment or promotion is implied.

## Route interval serialization correction

The trial `43f62381-e632-44cc-ae4b-f6a083b4f06f` failed because review moved
an arrival from offset 5 to 6 but left its preceding duration ending at 5.
The route independently encoded both starts and lengths. New reconciliation
responses edit only span lengths, in existing order; starts are serialized as
their cumulative sum. The AI must preserve positive whole-bar durations whose
sum equals its arc length, or receives an explicit mismatch diagnostic. This
arithmetic changes no note attacks, pitches, gates or musical material. Legacy
field edits remain readable, but new response schemas do not request starts.
Tests reproduce moving that arrival with compensating adjacent lengths and
check atomic failure for unequal totals. The blueprint is reused on relaunch.

## Measured support-voicing experiment

Completed `fa908e66-24e3-4c84-84b2-8c0f8de697c0` already writes the ensemble
jointly. Its sustained friction is therefore not evidence of serial isolated
track generation. The existing `reviewClubVoicings` path was bypassed by
first-pass audition. Unified Club plans now run that narrow existing review
before publication, with a four-minute global review deadline. Other musical
revision loops remain bypassed. The reviewer receives measured actual MIDI
targets and can propose support pitch edits; existing code protects attacks,
durations, velocities and melodic/rhythmic owners and retains the prior score
when proposals are unusable. No new writing instructions or note generators
were added. This deliberately changes what first-pass means for this marked
experiment: raw writing plus support-voicing review, not every final review.
Do not treat an enabled review as proof that all tensions were corrected.

## Phrase identity, sustained events and comparison artifacts

The next new plan archives a phrase_library with stable arc/reference IDs,
owner IDs, exact source notes and response-to links. Writers receive those IDs
with the current routes and accepted-MIDI memory. These are authored musical
references, not automatically repeated or transposed clips. This is the first
concrete identity layer, not a claim that every abstract phrase is now a fully
developed part of the final work.

Marked plans permit an authored event to release after a technical window,
inside its existing section and owner activity span. Attack eligibility is
unchanged. Schema, parser, containment and writer deadline agree on this rule;
the cell retains its exact authored length. No note extension is guessed. Section
boundaries remain limits: general cross-section ties are not implemented here.

Immediately before voicing review, a checkpoint exports before-voicing-review.mid
and its plan. The full-song.mid is the final version from that same generation,
so analysis can compare revisions without a confounding new note-writing run.
No Cloud deployment, no mainline promotion and no guarantee of musical improvement.

## Original voice responsibilities in reconciliation

`d3da4752-26dc-4e1c-ae92-86256ee6aea7` cast requested an initial protagonist
statement over the plateau and a developed return; the later layer described
that same voice as deliberately withheld until the final section. Reconciliation
previously saw only the later architecture, not the original cast roles. It now
receives those immutable responsibilities/allowed sections and may explicitly
correct a layer mode as well as its entry/release bounds. Existing full-plan
validation still checks consistency. No voice is activated or filled by host
code. Tests cover supplying original role context and applying an AI state edit.

## Shared tonal contract for references

Job `90856617-1ac0-4ebe-a0b6-3e0bc82c3005` has D-dorian MIDI but MIDI 66 in
arc 2's phrase reference. Creation, validation and targeted repair of references
now use the same AiChromaticPolicy as MIDI. Validation reports the exact arc,
reference and note through the existing focused repair path. Short-score note
arrays also share the policy. No automatic pitch substitutions. Tests reproduce
the F-sharp defect, permit F natural, and retain explicit chromatic permission.
This establishes a tonal contract, not a guarantee of compelling melodies.

## Short passage versus mandatory section count

The 16-bar trial `04d87c81-1882-43b8-9721-ab3fa6a6e6d3` returned two meaningful
eight-bar sections plus an empty third section with two arrivals at zero. The
macro schema required at least three sections and at least two events per section.
For Club passages of at most 16 bars the macro now permits one or two sections,
one held harmonic event and nonempty section names. Longer/non-Club schemas stay
unchanged. Duplicate-arrival validation remains strict; no chord is deleted or
chosen locally. The trial is regenerated, since its previous macro was invalid.
