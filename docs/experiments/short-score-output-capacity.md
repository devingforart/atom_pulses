# Capacity for complete compositional arguments

24e3c6f7 reached `incomplete/max_output_tokens` at the fixed 8,000-token ceiling,
before any reference was accepted or MIDI written. The existing reference schema
contains journey, continuation, ensemble excerpts and relationships for the cast;
a six-section ensemble needs more serialization space than a one-section sketch.

The initial short-score ceiling now scales with bounded section/cast counts,
from 8k to 28k. An actual provider `incomplete/max_output_tokens` permits exactly
one retry, raising only that request's output ceiling, up to 40k and within the
remaining generation time. Other failures do not trigger this capacity retry.
Partial JSON is never treated as a score; all existing note/reference validators
still run on the completed response. No note count, density or additional part is
required by this budget, and no local notes are generated.

This ceiling is not actual billed usage and does not guarantee quality or
completion. Small single-section/six-owner anchors retain the 8k allowance.
Tests cover baseline, larger ensembles, recovery growth and finite upper bounds.
Preparing a retry is also idempotent for the existing pitch-policy instructions:
transport recovery must not append the same musical contract a second time.
