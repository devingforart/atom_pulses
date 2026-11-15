# Lossless window-schema factoring

The fresh Club generation f1548fa4 failed at window 4/12 because its direct MIDI
schema exceeded the provider's 5,000-property budget. Three accepted writing
windows and their MIDI checkpoint remain intact. This was a request-format
failure, not a reason to reduce tracks, notes or musical development.

Direct-window schemas now share structurally identical object schemas through
root `$defs` / `$ref` and remove definitions unreachable from the effective root.
Beat alternatives, gate bounds, retained pitches, scale policy, monophonic slots,
nullable rests, repeats and note-array capacities are unchanged. The note decoder
and MIDI validators remain unchanged. This does not generate or simplify music.

Regression checks expand all finite references and require exact equality with
the original expanded contract. A joint 24-owner synthetic window verifies that
repeated constraints no longer exhaust the property budget without removing any
owner. A read-only diagnostic reconstructs the failed window from its accepted
cast/architecture and verifies equivalence and the provider property ceiling:

`pulso_club_nucleus_tests --inspect-window-schema JOB_DIRECTORY SECTION FIRST_BAR BARS`

The optional `--dump` prints only the compacted schema for a provider validation
probe. No credentials are read by this diagnostic. Definition sharing is not an
unlimited-capacity claim for arbitrarily distinct schemas; actual provider
acceptance and checkpoint preservation still need verification for a run.

Verification for f1548fa4: the complete-cast diagnostic had 5,530 properties
before sharing and 1,082 after sharing for section 1 / local bar 8 / 8 bars.
All 12 windows passed expanded-contract equality with 11 cast owners; the
largest compact schema had 3,002 properties. The provider accepted the compact
direct-window schema in a bounded validation probe, which was then cancelled.
Use section `-1` to audit every window with the diagnostic's supplied bar span.
