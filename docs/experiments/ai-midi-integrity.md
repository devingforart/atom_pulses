# AI to MIDI integrity audit

## Confirmed failure

For `932e9d2d-848f-4319-af59-d5224aa1d947`, retrieved original direct-window
responses already contain two bass attacks together and up to five simultaneous
lead notes. The second response supplies only three harmony notes but 111 lead
notes. The rendered checkpoint/export therefore did not originate this role
confusion. The parser did not enforce the AI plan's `polyphonic=false` contract.

The direct parser now rejects multiple attacks at the same timestamp for an
explicit monophonic owner. This sends the existing writing recovery the exact
owner and timestamp; it never deletes or redistributes chord notes. Intentional
legato overlap between different attack times is not prohibited by this check.

## Export verification

The worker rereads the complete MIDI and each per-owner MIDI after export and
compares event multisets against their rendered source patterns. Pitch, velocity,
attack and release must match after integer MIDI-tick encoding, with matched
note-off events. Per-track comparisons check the exported ownership separation;
the full-file comparison checks aggregate content. Mismatch stops publication.
Tests exercise exact round-trip and deliberate pitch/release discrepancies.

This is not a claim of complete semantic or musical correctness. It does not
prove that every AI intention was enacted, or that score normalization preserved
every input event. More cases are required for overlapping same-pitch events,
controls/expressions, channel mapping and transformation/repetition paths.
Existing phrase/scale/timing validation remains in force. No Cloud deployment.

The completed synthetic round-trip now covers three distinct owners: monophonic
bass, a three-note chord, and monophonic lead, with exact owner/pitch/time checks
after PerformanceScore rendering and file decoding. Its Pattern includes the
same part metadata required by the production exporter. Future successful jobs
persist `midi-integrity.json` containing the checked rendered events and number
of verified per-track files. Missing audit persistence also stops publication.

## Shared landing metadata

The next observed planning failure declared a joint groove landing not allowed
by both existing rhythm cells. This is plan metadata, not MIDI transport damage.
The bounded plan-repair dispatcher now addresses shared_landings alone. Its
schema lists actual eligible quarter-grid coincidences in the initial 64 beats;
the AI chooses which to declare, including none for nucleus-backed complementary
patterns. Application verifies the claims and preserves all rhythm/note fields.
Tests check invalid/valid coincidences and unchanged cells. No automatic rhythmic
alignment or new musical attacks are introduced.

## Preserve raw events during verification

Job `06032740-9dfa-473d-b9cf-158d74b7d415` has 1375 expected/exported notes
but a same-pitch G overlap at beat 312.5 (duration 4) and 316.25. JUCE's default
readFrom matching and updateMatchedPairs can insert an earlier note-off for
that overlap. Verification now reads raw events (`createMatchingNoteOffs=false`)
and pairs FIFO within each track/channel/pitch, consistent with the web MIDI
parser, without modifying the sequence being audited. A regression reproduces
the exact overlap and still rejects a changed duration. This verifies encoded
events, not universal DAW playback semantics for overlapping identical pitches.
Mismatch reports now include the first differing pitch/tick interval.

The following trial exposed a second ambiguity: a later equal-pitch attack can
release before an earlier sustained one. FIFO then associates the wrong release
with an attack, although the file contains every authored event. The audit now
compares exact attack (tick, pitch, velocity) and release (tick, pitch) multisets,
with orphan counts checked per track/channel/pitch. This still rejects changed
releases, attacks or velocities, without inventing note IDs that SMF cannot
encode. It does not certify equal-pitch overlap playback, track ownership or CCs.
Regression tests cover nested equal-pitch notes with different velocities.
