# Exactly one AI layer declaration per accepted cast owner

The completed phrase response for local job
`86cfeb9f-f48d-4722-b2c8-df979b46f1e2` repeated `pulse_high` with two different
registers and omitted `pulse_air`. An array of owner-specific `anyOf` choices
constrained each element independently, not uniqueness or total cast coverage.
Renaming the second layer would guess which musical decisions belonged to the
missing owner and is deliberately not done.

The provider boundary now represents layers as `layers_by_owner`, with exactly
one required object field for every accepted cast ID. Each value retains the
existing owner-specific layer schema, including functional identity, timing and
section-state constraints. The redundant inner `instrument_id` is removed.
Register schema bounds mirror the existing validator's allowed range; no range
is widened and no note is retuned.

Lossless decoding inserts the owner ID from its explicit key and restores the
canonical layer array in original cast order. It preserves all AI-authored
fields, rhythms, notes and other root declarations. Missing owners, conflicting
embedded IDs and mixed array/map formats fail atomically. Historical canonical
arrays remain readable and still undergo the original full validation.

The old combined owner/cycle/register error is split into precise diagnostics.
This change is general to Club layer planning, not a fix for one named pulse or
one generation. Tests cover exact cast coverage in the schema, preservation of
single-tone constraints and note/rhythm values, stable cast order, missing and
conflicting identities, and mixed-format rejection. Local only; no Cloud deploy.
