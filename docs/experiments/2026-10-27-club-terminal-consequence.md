# Terminal consequence, local A/B

Anchor: `5818cff2-3d21-424f-a766-cafadf2eb384`, listener score 33/45,
all dimensions 4 except ending 1. Full piano listening; usable, high confidence.
Listener also flags flat climax. Keep the approved production motor unchanged.

Observed MIDI: the hook holds D5 from beat 344 through 352, but the carrier
continues eighth attacks through 351.5; bass and stabs also keep cycling.
The final section describes continuous motion until its release boundary.
Thus a resolved lead note is not sufficient ensemble resolution. No claim that
this symbolic observation certifies auditory quality.

Local opt-in `PULSO_AI_CLUB_CLOSURE_EXPERIMENT=1` adds instructions ONLY for
the final section of a full arrangement. It distinguishes an outer release
bound from a compulsory last attack and permits AI-authored completion of
recurring gestures. No automatic edits, prescribed cadence, forced silent bars,
extra instruments, or production profile changes. Loop excerpts are excluded.

The next generation reuses the same macro/cast/nucleus, tempo, duration, prompt
and seed. It regenerates performed notes, so this is not a bit-identical interior
with only its ending replaced. Measure incidental changes before attributing
all perceived differences to the experiment. Stop the turn after submission,
as requested by the listener. Cloud and the Git anchor stay untouched.

Tests: feature isolation, interior-section exclusion, full-arrangement final
section inclusion, loop exclusion. The process follows the official OpenAI
prompt-engineering guidance to evaluate instruction changes against retained
references: https://developers.openai.com/api/docs/guides/prompt-engineering

## Listener rejected the first closure experiment

Job `f27d32a6-f46a-4475-9fa9-ed65f28cb3fa`: 1954 direct-AI notes.
Bass and stabs still retrigger until beats 350 and 349.5 respectively; the
sequence remains active until 349.5, and multiple sustained layers end at 352.
The additional writing paragraph did not produce a convincing perceived ending.

Second local iteration adds an AI-authored `terminal_resolution` to the
production plan: section-local preparation, arrival, resonance end, and each
owner's last attack/release and concluding gesture. The schema remains local
opt-in. Validation checks time coordinates/owners and the realized MIDI against
AI-declared terminal bounds. Violations return to the AI through the existing
bounded recovery path, with concrete evidence; no note is clipped, extended,
inserted or shifted. Chronology alone does not certify an audible resolution.

This comparison must request a NEW production plan (`replan_phrases=true`),
otherwise it would reuse the very continuous-to-the-end contract being tested.
Macro form, prompt, tempo, duration and seed remain from the approved anchor;
the newly authored production plan may change internal material as well as the
ending. Cloud stays at the approved revision.

## Developed farewell, third local iteration

The listener also rejected `f0f86ff1-ca70-4e99-8f38-44262fc57e30` as too abrupt.
Its plan starts preparation at section beat 20 and declares arrival at 24;
motion continues through 27.5 and releases as late as 30 out of 32 beats.
The lead finishes its opening statement around song beat 328, then returns
only at 344 on a held root. This leaves no developed melodic approach to the
declared arrival. Obeying lane deadlines alone was not sufficient.

The experimental plan now requires `melodic_answer`: an existing owner, its
relationship to the established motif, and an explicit AI-authored final phrase
in section-local beat/duration/pitch/velocity events. The writer receives this
phrase with the ensemble ending plan. Prompting distinguishes a developed
approach from a late root tag and asks motion layers to finish their argument
around the arrival instead of starting another cycle. No fixed cadence or
number of silent bars is added. Code validates the sketch's timing, register,
scale and self-declared release bounds but does not copy it into performed MIDI.
Actual realization and listening remain to be evaluated after generation.

Keep prompt, 180-second request, 122 BPM, seed and macro/cast from anchor
`5818cff2-3d21-424f-a766-cafadf2eb384`; replan the production/terminal material.
This is a whole-score regeneration, not an exact protected-interior repair.
No commit, deployment, production-profile change or automatic MIDI edit.

## Exact arrival realization, fourth local iteration

Listener found `6b72dc0f-e7fe-4c17-80ae-aa259ee5eb8e` clumsy. Read-only retrieval
of its last original OpenAI response (HTTP 200) confirmed that the response
itself wrote section-local 23.75 for lead, pads, sub and texture, while the
accepted plan said arrival 24. This was already in the saved checkpoint, not
introduced by Ableton or MIDI export. The API response's arp also retained
two pitches at 23.5. Existing upper-bound checking did not catch anticipations.

The terminal plan now declares `joins_arrival` per lane. Realization checks
require those AI-selected participants to attack at their declared arrival.
Older plans infer participants from last_attack_beat == arrival_beat. The
foreground's explicitly authored farewell is checked against performed
pitch/onset/duration in the current window (a sustain crossing a technical
boundary is checked up to that boundary). Violations return to bounded AI
recovery, never to a local note-mover. No other sections are affected.

The experimental planning/writing instructions also ask for directed phrase
articulation and perceptual separation from the sequencer rather than a
detached scale with same-register doubling. This is not guaranteed by the
technical check and still requires listening. Tests cover a sixteenth-early
arrival, a wrong arrival pitch, a valid arrival and audit immutability.

## Prepared ensemble withdrawal, 2026-10-28

Read-only analysis of `f6298cae-d654-419e-a81d-8d0d2034e4d9` confirms the
declared arrival at absolute beat 344. It also finds an unprepared-seeming
F77-to-D86 leap with velocity 112, an arp sharing the lead's register during
preparation, and a sub D38 at 320.25 lasting 24 beats overlapping its next
attack at 344 by .25 beats. These are different issues: exact plan compliance
does not certify a convincing musical ending.

The opt-in planning instructions now ask each lane's existing gesture field
to describe behavior throughout preparation, not just the final deadline.
They distinguish an earned climax from a concluding phrase and ask the AI
to consider a neutral-piano rendering when coordinating the arp and lead.
There is no mandated descending line, register offset, reduced note count,
cadence or silence length. The writer receives the same ensemble guidance.

A read-only final-section audit now reports same-owner/same-pitch overlapping
retriggers to the existing bounded AI recovery. It includes preceding sustains
crossing the current window boundary; different-pitch legato, chords and
touching note boundaries are allowed. It neither trims nor moves notes and is
disabled outside the local closure experiment. Tests cover overlap detection,
immutability, touching notes, different pitches, unrequested owners and flag
isolation. A successful technical check is not a musical quality verdict.

The next comparison reuses the approved macro/cast, prompt, seed and duration,
replans the closing/nucleus material and regenerates the score. It is not an
identical-interior ending edit. Cloud and the approved anchor remain unchanged.

## Resume failure: encode the AI's arrival in the output contract

Job `6b725962-e5d5-4962-a3b6-65ae7e00ba27` accepted ten windows (1,897 notes)
but exhausted recovery in window 11. Read-only retrieval of its final API
response `resp_0c5f87f1c89fbd0f006ae17007699887d19f95d66474faaa42`
confirmed the model itself wrote 23.75 instead of 24 for four joining owners.
The returned response schema explicitly allowed 24 with duration <= 7.5:
the arrival was not excluded by the schema or moved by MIDI export. The
old flat array permitted omission of an obligatory landing and relied on
post-hoc validation and repeated prose feedback to repair it.

In the local closure experiment, an owner joining an arrival inside the current
window now returns `{notes: [...], arrival_notes: [...]}`. The latter is required,
nonempty and uses the existing permitted note schema restricted to the exact
AI-planned arrival, translated to window coordinates. Pitch, velocity and duration
are still written by AI. Ordinary notes exclude that exact onset to avoid writing
the same landing in both containers. Other owners/windows keep ordinary arrays.
If the declared arrival is incompatible with allowed onsets, schema construction
fails explicitly before the paid writing call. Parsing checks both containers and
flattens them without manufacturing or changing any event. Same-pitch overlap and
musical realization checks remain in place; this does not certify musical quality.

Tests cover local-coordinate translation, window/owner/flag isolation, required
exact arrival, no shared-schema mutation, incompatible arrival, empty/anticipated
landing rejection, and preservation of authored events. Resume the failed job's
exact checkpoint with its existing phrase plan, not a blueprint regeneration.
