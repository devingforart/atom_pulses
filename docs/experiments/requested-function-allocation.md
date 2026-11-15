# Requested functions before MIDI authoring

The previous six-owner trial requested three static tonal rhythms, bass, a pad
and a protagonist, but cast two sustained bodies and only two rhythmic owners.
The cast prompt and validator had an unconditional two-body / 85% floor policy.
This was a real conflict before note authoring, not a MIDI renderer problem.

New Club manifests carry AI-authored requested_functions: verbatim user quote,
functional category, exact requested count (0 when unspecified), and owner IDs.
The host checks quote presence, counts, distinct existing owners and preserves
the mapping through blueprint assembly. It does not derive roles from a keyword
list or compose notes. Understanding the quoted sentence remains an AI task;
these checks cannot prove the model interpreted every user intent correctly.

Explicit allocations outrank the historical two-body floor. Unspecified legacy
requests retain their previous floor gate. Cast, phrase plan and note writing
remain the existing calls; no additional AI request is introduced. The phrase
schema binds requested static rhythm owners to static_tonal_rhythm and held
harmony to stable_foundation. A validation check catches substitutions before
MIDI. The mapping is saved in the phrase plan for audit and resumption.

Tests cover count mismatch, duplicate identity, invented quote, rhythm-to-pad
substitution, actual per-owner schema constraints, legacy compatibility and
nonmutation. Musical usefulness still requires inspecting the rendered score
and listening; matching role counts alone does not certify a club composition.

## Timing coherence

The next planning attempt made bed_dm9 both phrase_flexible and meter_carrier.
That is a genuine declaration conflict, not a failed MIDI quantization. When
explicit static rhythm owners exist, their schema now declares cycle_bound and
lists those owners as eligible carriers, excluding the sustained flexible bed.
The model chooses which eligible owner carries the meter and authors its whole
rhythm. The constraint does not insert notes, select beat offsets or force a
quarter-note pattern. Other owners retain their own permitted timing choices.
The old contradictory prompt saying every core owner must be cycle_bound was
removed; core importance is not a universal timing restriction. Error messages
now identify mode, carrier and blank intent rather than reporting one opaque
timing failure. Tests cover the constrained choices and preserved pad freedom.

## Carry the same identities into the initial short score

The successful follow-up preserved three constant-pitch layers, but an earlier
planning stage still had no structured knowledge that these owners were static.
It could ask one to deliver a melodic handoff, obliging later planning/writing to
choose between incompatible identities. The same saved allocation now reaches
both short-score and phrase planning. The short-score handoff schema excludes
explicit static rhythm owners as melodic recipients; ensemble_excerpt and
after_answer retain those owners for their rhythmic contributions. Null handoff
remains available, and is the only option if no non-static owner exists. No new
model call, automatic orchestration, source-note deletion or pitch adjustment is
introduced. Tests check legacy behavior, eligible recipients, static-only cases,
null handoff and preservation of supplied musical notes.
# User-source binding for cast generation

Cast requests and their existing bounded recovery now bind `source_quote` in
the structured response schema to the complete original user direction. This
is provenance, not a host interpretation of which musical functions to compose.
AI-chosen instrumentation remains in `instruments`; `requested_functions` is
empty when USER did not explicitly allocate independent functions. Macro or
Club guidance must not be represented as a user-imposed quota.

Validation keeps historical verbatim-substring quotations readable, but reports
source mismatch, invalid count, empty owner lists and actual count mismatch
separately. It neither drops ungrounded declarations silently nor changes MIDI.
Regression coverage includes generic `etude F#min`, multiple explicit functions
bound to one original request and the actual cast schema's source binding.

The pipeline now passes original user direction separately from augmented
composer context. Only the former grounds requested allocations. Source enum
values normalize whitespace because the strict structured-output service rejects
line breaks inside enum string literals; provenance validation uses the same
whitespace normalization and preserves historical substring citations. No
musical text or note events are synthesized by this codec. HTTP request failures
take precedence over missing-content diagnostics, and HTTP 400 does not trigger
an identical cast retry. API traces retain the provider error message/code.
