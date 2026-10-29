# Connected ensemble candidate

The user found the preceding variation subtle, with competing/simple melodic
material and insufficient long-note contrast. This local motor change adds
concrete foreground sentences to the existing musical arcs, rather than relying
only on prose development around an explicitly notated repeating groove.

Each experimental arc carries melodic_statement: AI-authored arc-local notes,
development, held_voice and ensemble_relationship. The foreground owner is the
arc's existing foreground_id. Intentionally instrumental arcs can carry no
foreground notes. No duration quota, forced track count, regular variation
schedule or automatic note augmentation is introduced. Existing voices can supply
the missing slower sustained strand and complementary answer without extra tracks.

The planner is instructed to coordinate these sentences with harmonic/layer
states and the terminal plan. Validation checks numeric/grid data, owner, span,
register and current tonal policy. The writer sees the arc plus window-relative
onset/release coordinates, including already-held notes crossing a window boundary.
It authors all actual events; code neither copies anchors nor lengthens notes.
Anchor realization remains a musical instruction, not a new exact-match gate.

Scope: all new Club jobs using the local candidate on port 4307 with the existing
closure experiment enabled. No saved MIDI edits, production deployment or commits.
The comparison reuses macro/cast/prompt/seed and 180 seconds, replans the musical
material and regenerates the score. Previous candidates remain untouched.

Tests cover schema/writer integration, held cross-window coordinates, unrelated
arc isolation, bounds, deliberate instrumental arcs, legacy compatibility and
production isolation. Musical quality remains unproven until listening.

## First run: explicit ending versus repeating attack filter

Job 8cb2b9c8-691e-499f-8c5d-4246f01a4906 saved ten accepted windows,
1,738 notes, then exited with code 3221226505 before logging the final window.
The saved plan asks cycle-bound stabs at local 1.5/3.5/5.5/7.5 to join the
terminal arrival at final-section beat 28. Schema construction removed that
onset under the recurring cycle filter, then withRequiredArrival threw an
uncaught exception. This incompatibility is reproducible without an API call.

Schema filtering and MIDI parsing now share windowAttackAllowed: preserve all
ordinary cycle rules but admit the exact AI-declared terminal arrival for a
joining owner in the final section, only with the local candidate flag enabled.
This does not add notes or alter rhythm-cell repetitions. Unexpected schema
exceptions in ensemble generation are reported as explicit failures with the
checkpoint preserved, rather than terminating the worker. Tests reproduce the
offbeat-to-terminal landing and cover other onsets, sections, nonjoining lanes
and production isolation. Resume from the ten-window checkpoint unchanged.
