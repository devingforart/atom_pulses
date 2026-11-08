# Discourse-first breath validation

Failure f15c25b4-d188-4036-ac2a-7cf9b1f863d5 retained five pass-windows.
At window six (zero-based bars 40-48), protagonist_lead was absent. The
secondary_counterline route explicitly declared breath at bars 40-49, despite
its section layer's broader reduced activity span. The old no-attacks guard
rejected the AI's empty note list five times before the accompaniment pass.

For persisted foreground_first_v1 plans only, a requested owner's entire
available interval can be silent when explicitly covered by breath spans.
An intersecting statement/continuation/arrival or uncovered interval prevents
this exemption. The already-existing silent-group path skips that paid call.
Layer omission checks recognize the same narrow exception. No musical notes
are generated, deleted or changed. Other modes retain their prior semantics.

The global window audibility check now counts notes sounding across the window
boundary rather than only new attacks. A genuinely missing promised phrase
still fails the appropriate checks. Native tests cover full breath coverage,
partial coverage, gaps, other owners and historical checkpoint behavior.

Resume the exact accepted score from five units using the persisted two-pass
schedule. This does not restart the macro, cast or accepted foreground MIDI.
