# Unique AI reconciliation decisions per real plan field

Local job `a46d481f-3167-4ed4-a5f1-7352e445e1ac` failed before MIDI:
the completed reconciliation response proposed three edits of `arc[0][0].bars`
with values 6, 6 and 8. Keeping the first/last or silently deduplicating these
would make the host choose between conflicting musical declarations.

New requests use a schema generated from the actual accepted plan. Each existing
whitelisted field has one unique `edits_by_target` key. Its value is null (keep)
or one final AI-authored number/text. There are no free-form indices, sequential
edits or note-array targets. Numeric, action and activity types remain bounded;
direct route-start edits are not offered. The output capacity accounts for the
nullable map's serialization overhead without changing musical content.

The decoder translates non-null values into the existing atomic edit validator.
Unknown/missing keys, mixed encodings, unauthorized values, invalid route sums
and invalid whole-plan contracts still fail without partial commit. Route
positions are serialized from AI-chosen durations as before, never composed by
the host. Reference notes, pitches, cast and harmony remain immutable. Historical
edit lists remain readable, and their duplicate targets remain rejected rather
than resolved with a first/last-wins policy.

The post-reconciliation acceptance path also rechecks functional allocation,
not only the general phrase architecture. Tests cover no-op maps, exact AI value
translation, reference preservation, invalid targets, missing keys, mixed
formats, and legacy duplicate rejection. Local only; no Cloud deployment.
