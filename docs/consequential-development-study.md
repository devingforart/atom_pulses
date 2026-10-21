# Consequential development, local experiment

## Hypothesis and scope

The joint-groundwork anchor is `The Held G Answered`:
`0b6f1bf1-51ed-42a1-80f8-da3a9a2eaa4d`, on port 4204.
Listener feedback: clearer but square, insufficiently developed and less dynamic
in its musical behavior. MIDI velocity variation actually increased. Do not treat
this as a request to randomize velocities, force asymmetry or add subdivisions.

The new experiment separates recognizable identity from the continuation and
ensemble relationship that may develop. It is generic and does not reference
any commissioned track, bar, key, note or source MIDI. No actual score repair.

The macro schema adds a per-section `development_strategy` containing retained
identity, changed relationship (purposeful stability is valid), ensemble behavior
and an audible consequence. Decisions follow the jointly authored reference
passage. Phrase planning receives the full section-linked decisions and enacts
them through existing `material_development` fields, without extra phrase quotas.
The stored phrase plan carries the same decisions to the joint ensemble writer.

These are AI-authored compositional instructions and a structured representation,
not a procedural musical engine. Every realized pitch, rhythm, duration, rest and
velocity remains authored by the existing AI writer. There is no algorithm for
expanding, varying, thinning, filling, displacing or humanizing notes. Technical
validation checks descriptive field integrity only; it does not judge whether a
passage is expressive or reject intentional repetition.

`PULSO_AI_CONSEQUENTIAL_DEVELOPMENT_EXPERIMENT=1` also requires the groundwork and
development experiments. Default launcher: `node web/local-studio/start-consequential-development.mjs`
(port 4205). Ordinary worker environments strip this flag. Accepted Cloud/VST
defaults remain untouched. No commits or deployments are part of this experiment.

## Validation plan

Generate fresh works, with different seeds and stylistic requests, rather than
editing the anchor until it sounds better. Evaluate the final MIDI independently
of its narrative labels: identity retention, meaningful continuation, ensemble
hierarchy, expressive change beyond volume and an earned ending. Repetition,
symmetry and steady pulse can be correct, particularly in electronic music.

Technical tests establish representation/isolation, not musical quality.
The design uses [OpenAI structured outputs](https://developers.openai.com/api/docs/guides/structured-outputs)
for explicit data shape; this is not an artistic guarantee.

New runs and observations are appended below. A small test set is exploratory;
listener approval across multiple works is required before replacing an anchor.

## Runs, 2026-10-20

All are fresh AI scores, first-pass joint ensemble, Terra/medium, adaptive. No
blueprint reuse, source-MIDI editing, rehearsal or procedural musical changes.

| Arm | Job | Prompt | Seconds / BPM / seed |
| --- | --- | --- | --- |
| Development | `bb7bb4f7-ed46-477d-9fb0-01f47d1cf1d1` | `Musica como la de mozart.` | 180 / 120 / `202610200101` |
| Development | `88e496ec-6e46-4898-b24a-2a7fc0e4f0db` | `Musica como Bela Bartok: musica de camara oscura, con pasajes entendibles, voces relacionadas, desarrollo y final claro.` | 120 / 100 / `202610200102` |
| Development | `4074bab2-34cc-44f3-9e46-21339711a278` | `Progressive house oscuro e hipnotico, unicamente armonias, bajo y melodias, sin baterias ni percusiones. Desarrollo gradual y final claro.` | 120 / 122 / `202610200103` |
| Groundwork control | `968e2581-1ab5-4b85-9faa-ca668b1bac34` | `Musica como la de mozart.` | 180 / 120 / `202610200101` |

Lab ports: 4205/4206/4207 new decision-based experiment; 4208 groundwork-only
control. Same shared local library, all in Auto. The control retains the same
prompt/seed/duration as the first experimental work but API inference is not
guaranteed deterministic: one pair cannot establish a universal causal effect.
Do not interpret distinct section lengths or lower density as automatic success.

### Representation fault found before musical realization

The first classical/chamber runs and the groundwork-only control stopped before
MIDI with `Groundwork phrase names an unknown role or invalid local span`.
The model used descriptive `speaker_role` aliases (`subject`, `principal voice`,
`subject with bass reply`) instead of repeating the exact declared voice role.
No notes were rejected on artistic grounds. Exact original responses are retained
as `failed-macro-response.json` within each ignored job directory, retrieved from
their existing Responses IDs without another generation request.

New schemas use `speaker_voice_index`, an explicit zero-based reference to the
same passage's voices array. Validation checks integral values and actual array
bounds, never guesses a voice from prose, and never changes notes. Previously
accepted checkpoints with exact legacy `speaker_role` values remain readable
verbatim. The musical experiment and control both receive this transport-contract
correction; their difference remains the development strategy flag.

Regression cases cover valid index zero, out-of-roster and fractional indices,
legacy references, and immutability of the authored passage. We do not infer
musical success from correcting this schema fault. Retry IDs follow below.

Retries using explicit indexed references:

- Classical experiment: `fd43ef6d-1f82-40a0-b8c8-78c0b3592241`, 4205.
- Chamber experiment: `4b262950-10a6-4292-93d1-06b4ffa075e7`, 4206.
- Classical groundwork control: `9482f45f-25fe-4c91-b4ef-3449424896ca`, 4208.

The electronic run `4074bab2-34cc-44f3-9e46-21339711a278` completed under the
earlier reference schema: its exact role references were valid. The change to
reference indices does not alter the musical policy or MIDI. It produced
`Pregunta de D, Respuesta de Sombra`: 125.902 actual seconds, eight populated
tracks, 491 direct-AI notes and no unterminated notes. Six section-linked joint
development decisions survived into phrase planning/realization. The 21-note
principal reference occurs exactly at the start; other passage roles are not
literal copies. None of these facts establish narrative or auditory quality.

## Complete results and reservations

All three styles and the control completed. The classical experiment required
one bounded technical retry of its last writing block: the AI wrote a final
release two beats beyond the work. The AI returned the corrected block; no
mechanical clipping or musical repair was performed. Other completed runs had
three/five writing calls matching their respective writing-window counts.

| Work | Arm | Actual length | Populated tracks | Direct-AI notes |
| --- | --- | --- | --- | --- |
| The Restored Breath | Classical development | 176 s | 7 | 810 |
| The Descent Answers | Classical control | 176 s | 8 | 976 |
| The Sixth Turns Home | Chamber development | 115.2 s | 8 | 396 |
| Pregunta de D, Respuesta de Sombra | Electronic development | 125.902 s | 8 | 491 |

All other note-provenance counters are zero. No unterminated notes in any score.
The smaller/longer real durations follow the existing complete-bar planning at
the requested BPM; no time stretching was introduced by this experiment.

`The Restored Breath` carries all seven section-linked development decisions
unchanged into phrase planning. Its complete 45-note main reference and seven-note
bass reference occur at beat zero. In the middle, oboe actually takes over:
principal/oboe attack counts by section are 45/10, 55/15, 9/51, 0/60, 72/12,
60/10, 25/0. This establishes a real transfer of foreground, not proof of a
convincing conversation. The return/climax is led by violin. The ending recalls
the melodic material and settles on C/G with the last C held 172–176 seconds.
Important reservation: 140 of 142 bassoon notes are two beats long, and most
principal/oboe notes remain half/one beat. Surface regularity has NOT been proven
solved. Its sections are 16/12/12/12/16/12/8 bars; unequal lengths are not a grade.

The control also preserves a full thematic statement and has a clear C-major
terminal triad. It cannot be dismissed as incoherent on the basis of being the
control. Its melodic/arrangement differences and stochastic inference mean one
pair is exploratory, not a statistically established quality improvement.

`The Sixth Turns Home` preserves almost all of its opening 37-note contour and
timing, with changes around beats 12–13. The absence of a full exact-reference
match is therefore not evidence that the theme was lost. Five development
decisions are carried exactly. Its final sonority includes D/F/C/E, a deliberately
colored D-minor ending rather than a bare triad; human judgment of closure is
needed. All roles in this score end with valid MIDI releases.

Electronic reservation: one same-pitch overlap in the protagonist at absolute
beats 191–192.5 and 192–193.5 (A4, about 1:34.43). It was not automatically deleted,
aligned or classified as an artistic error; the potential reattack/release issue
must be considered in listening. Its steady motifs and colored final Dm9 need
not be replaced by continuous novelty to pass an arbitrary development quota.

Read-only tools: `analyze-thematic-groundwork.mjs` verifies reference/context
transfer; `analyze-phrase-development.mjs` describes actual notes and repetitions.
Neither grades or rewrites music. Unit tests cover index integrity, legacy
compatibility, intentional stability, immutable reference data, strategy transfer
and disabled-path isolation. Node profile/checkpoint suite: 17 passing cases.

Status: implementation and multi-style generation completed locally; musical
superiority remains unconfirmed pending human comparison. No automatic promotion,
production-default change, commit, deployment or source-MIDI editing.
