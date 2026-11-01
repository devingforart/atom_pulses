# Club planning contract: complete revisions

Local only; no deployment or musical note transformation.

Job `d7d2f5f8-d585-4e76-b79b-45f49b1eb546`, prompt `progressive house violento`.
Retrieved both completed Responses using their recorded IDs (no new generation).
The first response contains all nine cast owners and full_arrangement. Offline
replay against the normalized blueprint identifies PHRASE_REGISTER_CONFLICT:
pulse_sequence, arc bar 72, local beat 49, pitch 55 outside declared 57..76.
The second response contains only pulse_sequence: a partial correction rather
than the complete production plan. The old combined validation error obscured
this second failure.

Changes: bind layer, state and trajectory array cardinalities to the fixed work
in the request schema; distinguish JSON/kind/format/context/cast failures and
report missing, duplicate and unexpected owner IDs. Club fallback reviews receive
their previous draft and explicit complete-plan correction instructions. Existing
focused register review is retained. No MIDI pitch, onset or duration is changed
by this fix. Duplicate owner identity remains checked semantically; equal array
length alone does not establish coverage.

Offline diagnostic replay:
`pulso_club_nucleus_tests.exe <absolute blueprint.json> <absolute response.json>`.
Normal invocation runs the regression suite. Rejected response fixtures remain
in ignored build storage, not source control. A fresh end-to-end AI generation
is still necessary to establish success for a new composition.
