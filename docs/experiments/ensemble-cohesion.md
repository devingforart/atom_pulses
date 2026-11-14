# Shared ensemble argument — local candidate

## Simultaneous voice planning candidate

After `e7bb6855-a4ab-4955-978b-626efef214bb`, the bass handoff was connected but
exported harmony still contained sustained internal semitone/compound-ninth
contacts. Such intervals are observations, not automatic musical errors. The
short score now supplies `vertical_plan`: foreground landings, compatible support
voicings and intended tension/resolution. Both arrangement planning and the real
window writer consume this context. Validation checks only complete structure;
there is no dissonance threshold, pitch correction or automatic note removal.
The previous timing/handoff work remains unchanged. Tests cover schema inclusion,
valid/invalid plan shape, immutable validation and real Club writer propagation.
This is an AI planning/writing change, not a post-generation harmonic repair pass.

## Receiver timing reconciliation

In `6e66390d-3976-48b1-9c01-91d7f025b36d`, the bass was assigned an eighth-note
answer while its `cycle_bound` grammar admitted only quarter-note attacks.
At bar 49 the exported bass had two simultaneous notes, followed by an absence
until bar 57. This is a conflicting plan, not evidence that the grid needs to be
disabled. The planner now explicitly reconciles receiver rhythm grammar and the
whole answering sentence. The real writer also receives the current handoff
beside that receiver's timing/polyphony/activity contract. It must author a
compatible complete sentence rather than stack/drop the source notes. Context
selects only the current section's handoff; this does not schedule or modify MIDI.
Tests cover propagation through the actual writer and exclusion of future handoffs.
Musical improvement remains an experiment, not a guaranteed consequence of the fix.

## Secondary sentence candidate

Following `4a4e559a-2a14-4165-b526-2161f0495ac2`, the local experiment asks the
short-score composer for an optional `handoff`: source/receiving cast IDs, the
inherited gesture, concrete answering notes and preparation of the return.
The layer planner must assign that answer an active speaking span and concrete
arc; the actual Club writer receives the same evidence through the repaired
shared adapter. Null means an intentional rest/plateau or no transfer, not an
instruction to fill silence. No new tracks, automatic note placement, quota or
extra API call is introduced. Legacy short scores omit the field safely.
Tests exercise cast-bound schema, known/unknown owners, self transfers, malformed
notes, null compatibility and handoff propagation through the real MIDI prompt.

## Integration defect found after Retorno Dórico

The actual `jointWindowPrompt` Club branch returned early after calling
`club::production::windowBrief` directly. Unlike the non-Club path, it never used
`AiPhraseArchitecture::windowBrief`, which adds `cohesion::context`. Consequently
the new short score, continuation notes, ensemble arguments and section-position
context influenced planning indirectly but were absent from the actual Club MIDI
writer. Helper tests passed because they tested context construction rather than
the real writer entry point. Prior claims of direct writer integration were wrong.

A regression test now calls `AiComposer::directWindowPromptFor` with a persisted
Club short score. It failed on the old code with `actual Club MIDI writer bypasses
short score and section position`. The fix routes Club through the shared adapter,
preserving Club-specific context and exposing the new material in both initial
writing and focused retries. No new musical instructions, note generator, quota,
extra API call or silence-removal rule is added by this fix. Existing activity
spans and AI-authored breaths remain intact. New tests also verify concrete
continuations survive retries and prompt construction leaves the plan unchanged.

## Continuation candidate after Horizonte Sumergido

`08cf7aa6-8fb8-4473-b45b-886f6741f28c` delivered concrete openings but reduced
many intervening melodic bars to C–D reminders. The next short-score schema asks
for a second AI-authored sentence (`continuation_notes`) and a `development_route`
per passage. Existing planning derives arcs and layer states from both sentences.
Every writer receives its actual section-local window bounds and whether it is
opening, interior or closing, alongside the unchanged accepted-note context.
This supplies material and continuity for the middle instead of more adjectives
about an opening/coda. There is no automatic placement, minimum activity rule,
note insertion, quantization change or extra API phase compared with the preceding
candidate. Legacy short scores remain readable. Tests cover required schema fields,
valid/invalid continuation notes and interior/closing context. Listening remains
necessary; a new random seed prevents attribution to this change alone.

## Next candidate: compose material before assigning layers

After `0b6bfa52-3b74-4533-a7c3-9ad4880e16a4` the user still heard
accompaniment rather than a complete work. The next candidate adds one bounded
AI call after macro/cast and BEFORE phrase/layer planning. It composes a short
score: concrete pitch/onset/duration sentences, connected development, intended
arrivals and simultaneous support responses. The existing planner derives its
arcs, scene relationships and layer states from this score. It is retained as
`compositional_short_score` inside the persisted phrase architecture and provided
to subsequent writers. Exact checkpoint resumes reuse it without regenerating.

This is a real additional phase, not merely new prose in the old prompt. It adds
latency and API cost (one call, output cap 8000 tokens, time cap 240 seconds within
the existing overall budget). Reference notes are never rendered, automatically
looped or substituted into MIDI by the host. Structural validation is not a
musical-quality claim; the result needs fresh listening. Old checkpoints remain
compatible. The full macro and cast still precede the short score; this experiment
reorders musical-material planning relative to layer planning, not the entire pipeline.

Baseline: `845e352f-262c-466b-942d-f13e25e0cf54`, Profundidad Dórica.
The exported MIDI has 96 bars, 737 harmony notes, 377 bass notes and 25 melody
notes sounding in only 10 bars. This is evidence of sparse focal participation,
not a universal minimum-activity rule. The user's listening assessment is that
the result feels like accompaniment rather than a complete composition.

## Engine change

The normal plan-aware phrase architecture schema now requests `ensemble_arguments`
for both Club and non-Club composition. Each section identifies a shared thought,
inherited question, foreground owner, shared arrival and participating owners'
phrase paths. This is generated within the existing planning call, not an extra
generation or a MIDI postprocessor. Each writing window receives its current,
previous and next arguments, retaining the relationship across technical chunks.
Club macro instructions also distinguish a full instrumental work from backing
for an imagined singer. Explicit loop, accompaniment and drone requests remain valid.

Only structure and owner/section references are validated. There is no musical
score threshold, coverage quota, automatic melodic duplication, silence filling
or note rewriting. Existing checkpoints without the field remain valid. All
notes remain authored through the existing AI writing path. Existing harmonic
cycle resolution remains in place. No Cloud deployment is part of this test.

## Verification and listening

ClubNucleusTests covers schema inclusion for Club/Adaptive, solo compatibility,
unknown references, duplicate sections, immutable validation and legacy context.
Generate fresh with the baseline prompt, 180 seconds, 124 BPM, Club, 32–64 horizon,
three requested tracks and a new random seed. Keep first-pass listening enabled.
Compare complete statements, audible responses, harmonic/bass coordination,
intentional plateaus and connected ending; do not call increased density success.
The new seed means this is not a controlled same-score A/B. Musical improvement
remains unproven until MIDI analysis and user listening after completion.

Schema conventions checked against OpenAI Docs:
https://developers.openai.com/api/docs/guides/structured-outputs

## Coordinated ending experiment

In `Pedal de Medianoche` (job `7b7864b8-7091-4f67-a8e7-c8bbc88179d4`),
the final melody releases at beat 376, exactly when the terminal supporting
chord enters at bar 95. That can be intentional, but does not by itself establish
an audible shared arrival. The short-score plan now asks the AI for an
`ensemble_ending`: prepared phrase, shared arrival, speaker release and what
remains afterward. Final-section writers receive this plan explicitly.
This does not extend notes, force simultaneous releases, fill rests or impose
a final-bar cadence. Legacy plans remain readable. Regression tests cover
validation and delivery to the final writer without leaking the terminal
directive into the opening section. Musical benefit still requires listening.

## Shared-clock voicing evidence

Follow-up `Orbita de A` (`56120747-ab14-43be-9279-c2f6c23098d6`)
contains a coordinated ending: melody A4 starts at bar 93 with A3/E4 support
and releases after that support. However, harmony sustains MIDI 64/65 together
for 16 beats at bars 45 and 85. The short-score prose calls the compound
64/77 overlap a minor second and describes releases not enacted by the held
inner voices. This is evidence of a gap between verbal voicing and actual notes,
not proof that dissonance itself is invalid.

The general short-score schema now requests a bounded `ensemble_excerpt` per
journey entry: existing owner IDs, pitches, starts and durations on one shared
local clock. It illustrates a representative phrase/arrival, not a complete
section or a prescribed loop. Both the architecture planner and current-section
writer receive read-only measurements of semitone-class overlaps and same-owner
same-pitch retriggers in this reference. The AI chooses their musical treatment;
the host does not reject tensions, edit notes or render the reference. Existing
plans without this field remain supported. Regression coverage includes compound
intervals, exact release boundaries, owner identity, immutability and delivery to
the actual Club writing prompt. The new run uses the same request with a new
seed, so improvement cannot be causally attributed from one comparison alone.

## Continuity after the answering phrase

`Profundidad Retenida` (`4ad6caf1-ca16-4a42-a501-6eda692ec275`) retains the
coordinated arrival at bar 93 and has no measured sustained semitone-class
overlaps. Its bass develops the planned response at bars 53-60, but returns to
root-pattern backing at 61-72 with the lead absent. This is not intrinsically
wrong for Club; the experiment targets the user's request for a developing
instrumental argument rather than accompaniment.

An AI-authored handoff now includes `after_answer`: the existing voice carrying
the consequence, what remains musically pending or deliberately settled, how
the next entry responds, and the pulse that stays stable. Architecture planning
must reconcile that continuation with layer states. Current-section writers
receive the consequence explicitly and consult accepted music to distinguish
an unfinished reply from an already completed one. There is no imposed activity
minimum, bar interval, automatic fill or extra track. Legacy handoffs remain
valid. Tests cover owner references, missing intent, immutable validation and
delivery to the real writer only in its relevant section.

## Plan versus performance: shared execution clock

Job `2d029239-0cb4-431a-b800-049b8b719599` exposes a contradiction before MIDI:
its terminal arc schedules the arrival at zero-based bars 84-88 and breath at
88-96, while its scene promises speech through bar 91 and sustain until 95.
The prose names tonic support during that arrival, but the harmonic timeline
puts the final tonic at bar 89 (one-based). Layer release bounds also put the
lead before the support, despite prose asking for the reverse. The rendered
lead ends at beat 340 and the ensemble at 356, leaving 28 beats empty. This
cannot be diagnosed merely as an exporter truncation or a missing adjective.

`AiPlanExecution.h` projects route-relative bars, section-relative activity
limits and the harmonic map onto a shared absolute-beat clock. The planner sees
the authoritative harmonic schedule; each Club writer (including retries) sees
past/current/future route spans alongside the actual chords in those spans and
each owner's observed accepted releases. In particular, a promised tonic in
prose no longer has to be mentally compared with a distant schedule. The ledger
does not claim to understand arbitrary prose or guarantee that the AI obeys it.
It does not revise accepted notes, force fills, reinterpret rests as activity,
or introduce a musical rejection threshold. The remaining uncertainty is AI
realization, to be assessed on a fresh work rather than declared solved.

Regression tests cover non-4/4 clocks, exclusive chord boundaries, past/current/
future separation, a sustain crossing a writing boundary, immutable MIDI,
legacy compatibility, and actual writer/retry integration. No Cloud deployment.

## Pre-writing AI reconciliation

`Gravedad Retenida` (`a0da3c21-c90d-4a6e-9885-01383d8542a0`) still pairs
terminal prose at bar 94 with an earlier numeric arrival route. Its melody
releases before bar 91 while the reduced final chord begins at bar 95. The
execution ledger alone did not resolve the contradictory instructions.

New Club plans now undergo one AI declaration review before any performance
window is requested. The response contains only indexed, whitelisted edits to
arc/route timing, arrival descriptions, section activity bounds, scene voice
spans and the declared ending. References, groove, cast, harmony and all note
arrays are immutable. An empty edit list is valid. Application is atomic and
the complete candidate must pass the existing architecture validator. There is
at most one retry for an invalid/incomplete response within the global budget;
failure stops before spending on MIDI, without silently accepting invalid edits.
The original accepted architecture remains unchanged until a valid candidate
is available. The review assessment and edits persist as `plan_reconciliation`.
Existing resumes do not receive an unsolicited replan. This adds an API review
and does not guarantee musical compliance; the next run tests that hypothesis.

Tests cover a valid timing repair, preserved note references, no-op review,
invalid indices, fractional bar positions and atomic unauthorized-edit refusal.

### Reconciliation contract regression

Job `4fd32bad-1a58-4a96-8655-8a7660db9ec0` failed before MIDI. Both remote
responses completed successfully. They requested `scene_voice.material` edits
to replace an inaccurate chord description; the flat response schema allowed
that scope/field combination but the local applicator did not. This was a
software contract mismatch, not a musical-quality rejection or a transport error.
Scene material prose is now an allowed edit (reference note arrays stay immutable).
The schema uses typed alternatives per scope and value type instead of a global
cross-product of fields. Invalid-edit errors identify the target and reason,
so a bounded retry receives actionable feedback. Regression coverage reproduces
the rejected scene-description change and checks restricted schema combinations.

### Bounded short-score source correction

Job `476b293c-3a0f-49bf-819d-49fbc575faea` failed earlier, during short-score
validation, not plan reconciliation. Its completed response names `bajos_mid`
as both handoff source and receiver in journey 1, while its prose says that the
bass answers the melodic D5 summit. The short score is now preserved while one
bounded AI call corrects only ambiguous `from_owner` references. A per-target
schema excludes the receiving owner; application is atomic and validates the
complete result. Every note, receiver and other declaration remains unchanged.
No local heuristic chooses a source. Solo ambiguity is not repaired by inventing
another voice. Unresolved references report their journey indices explicitly.
Tests verify exact preservation except the source field and immutable failure.

### Background timeout is not invalid music

Job `eecc53b6-ca8f-4c3f-add4-ea86ca0b55ac` reached the 240-second short-score
deadline while its remote response was still `in_progress`. The transport then
cancelled it (confirmed remote status `cancelled`), but the caller incorrectly
reported invalid short-score structure. A shared completion gate now requires
`completed`, successful HTTP/transport and no timeout/cancellation before short
score validation, handoff correction or reconciliation application. Pending,
incomplete and failed responses cannot be mistaken for invalid musical content.
The short-score phase now polls the same response for up to 480 seconds, still
capped by the existing global/recovery budget; no second create request is issued
to extend that wait. This is bounded tolerance, not a speed improvement or a
guarantee of completion. The failed call itself cannot be resumed after remote
cancellation. The next trial reuses its accepted blueprint to avoid repaying
for the macro form and cast. Tests exercise all nonterminal/error states.

### Route action contract

Job `77f15458-55fb-4cbc-b491-51aa13863a1d` completed both reconciliation
responses but the last proposed actions `response` and `preparation`, neither
supported by the existing route validator. Reconciliation now gives action its
own typed schema branch with the exact existing four-value enum. Its applicator
also checks that enum with target-specific feedback. Bar edits use integer
schemas, while layer beat boundaries remain numeric. Regression tests reproduce
both rejected labels, verify atomic refusal and allow the AI to express their
musical function as a valid continuation without modifying reference notes.

### Independent invalid fragments must not starve each other

Failed register trial `c36169a7-50fc-4146-b3b5-930ae369e936` spent its two
focused repairs on melodic references, then exposed MIDI 71 in a nucleus lane
whose range was 48-69. The global two-fragment loop prevented its existing
AI lane-repair path from running. Recovery now keeps the evolving candidate
and permits two attempts per named fragment, at most eight total calls under
the existing time/spending limits. This does not relax register/scale checks
or change a pitch locally. Tests reproduce two corrected references followed
by a chord-lane defect and enforce both per-target and global bounds.

## Voicing beneath expressive holds

Completed job `277dd7d3-5f28-46d6-97d3-55cdf69401f4` has a coordinated terminal
arrival, but several E/F and B/C overlaps between accompaniment and melody.
Those intervals are listening candidates, not automatically invalid notes.
The next candidate adds a read-only list of immutable voices held across each
window entry, with exact window-local release times. Within the existing joint
writing call, guidance asks the AI to choose foreground arrivals before voicing
the accompanying extensions and to check new simultaneous durations as well.
This is prompt/context engineering, not an additional critic call or a MIDI
correction algorithm. No dissonance gate, note-length floor or automatic omission
is added. Tests cover exact release boundaries and actual writer integration.
The previous worker is retained as an anchor; musical improvement remains a
hypothesis for the new-seed listening comparison.

## Expressive register experiment (staged)

The last lead could use MIDI 62-86 but wrote 69-81. This does not prove an
octave-wide restriction or that a wider melody would be better. `register_route`
now asks the short-score AI to choose a motivated tessitura trajectory or an
intentional narrow plateau and enact it in its reference phrases. The current
writer receives that choice explicitly. Existing range limits remain unchanged;
no minimum span, fixed octave shift, automatic transposition or high-note quota
is introduced. Historical plans without the field remain valid. Tests cover a
narrow choice, immutable validation and current-section writer delivery.

Prepared while job `a01d5c16-e5d6-43e7-a086-893b226d136a` is running. Its worker
binary is not replaced or cancelled. The experimental build remains staged in
`build-development/Release` until a separate trial is scheduled.
