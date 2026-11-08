# Reference-plan recovery

Failure: 7cf4048d-9703-413d-a85d-9dea50204d48 stopped before MIDI writing.
Both planning API calls completed successfully, but local reference validation
reported timing/register failure for protagonist_hook without distinguishing
the exact reference, note or constraint. The articulation reviewer had not run.

Changes: initial arc schema ties both references' pitch ranges to the selected
foreground owner. Validation reports arc index, reference field, note index,
actual note, reference length, register and constraint category. Note failures
enter the existing bounded fragment-repair path instead of regenerating the
whole plan. The focused schema binds each quarter-beat onset to its remaining
duration and the actual register. Only the selected reference's notes can be
replaced; the host does not alter pitches, gates or rhythm. All other fields
are preserved, then the complete plan is validated again. Two focused attempts
remain the maximum; no unbounded recovery or acceptance relaxation is added.

Native regression tests cover detailed targeting, bounded repair schema,
full-plan validation, immutable unrelated fields and unrelated errors. The
saved positive anchor 92b8f02 is unchanged. This fix is local, not deployed.
