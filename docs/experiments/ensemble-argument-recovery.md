# Ensemble argument integrity and bounded AI recovery

Ensemble arguments are relational metadata: exactly one per macro section,
existing voice IDs, unique contributions, an actual foreground contribution,
and nonempty musical intentions. Schema-valid arrays can still duplicate a
section or omit the selected speaker from its contributions.

Diagnostics now identify the entry, field, missing speaker, or duplicate owner
instead of returning one ambiguous message for every failure. A completed
phrase-plan response with invalid ensemble arguments receives one bounded AI
metadata correction, under the same model and request-wide deadline. Only
`ensemble_arguments` may be returned; extra fields are rejected. The host
preserves uniquely identified original arguments that already validate, and
merges the corrected metadata without modifying layers, arcs or reference notes.
The full architecture validation still runs after this merge. An unsuccessful
metadata recovery does not trigger a complete musical replan for the same error.

Checks cover precise diagnostics, valid-argument preservation, immutable layer
notes, invalid owners, extra-field rejection and atomic failure. This does not
claim that a previous failed draft's exact offending field was retained: older
diagnostics did not record that detail. Future failures report it directly.
