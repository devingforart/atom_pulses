# Bounded queue residence, not duplicate composition

The failed 23888cea request remained `queued` for both the initial 120-second
macro request and its new 180-second recovery request. Neither reached MIDI.

Background polling now accounts for queue residence separately from the existing
work allowance. A single response ID receives up to three minutes of queue time;
active processing retains its existing per-call work limit. A stricter global
automatic-recovery deadline always wins. Queue time can accumulate across state
transitions and is never credited as unlimited reasoning time.

Queue expiry cancels that response and records `queue_timed_out`. If and only if
it was still queued, cancellation is confirmed by the provider, sufficient time
remains and the user did not stop, one direct (non-background) attempt uses the
same request with only `background` changed. It remains inside the original
work-plus-queue/global ceiling. The cancelled attempt and direct attempt have
separate API trace entries; an unconfirmed cancellation never starts a duplicate.
Direct requests receive the existing work budget, not the 30-second background
submission budget. Direct mode cannot recursively enter queue fallback.
After a direct recovery actually completes, that worker process uses direct
transport for subsequent stages. This avoids repeating a proven stalled queue
route at every call; it is not a persistent model or musical-policy change.

A busy provider can still prevent generation; this does not guarantee latency
or availability and does not alter model, prompts, notes or musical validation.

Monotonic-clock regression tests cover queue credit, active work, queue expiry,
global caps, unchanged immediate-processing budgets, cancellation/stop guards
and preservation of all request fields except background transport.
