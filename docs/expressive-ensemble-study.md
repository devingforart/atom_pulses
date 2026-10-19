# Local expressive ensemble study

`PULSO_AI_EXPRESSIVE_ENSEMBLE_EXPERIMENT=1` adds an editorial direction to
the AI phrase planner and joint ensemble note writer. Local Studio forwards
it only when `PULSO_LOCAL_EXPERIMENTAL=1`. The shared production profile does
not enable it.

The experiment asks the AI to develop complete continuations, purposeful
register journeys and interactions between independently authored voices.
The existing phrase fields carry those intentions into the writer. The AI
still authors every pitch, attack, duration, rest and cadence. There are no
new octave quotas, generated arpeggios, automatic transpositions, note
fillers, genre-specific patterns or publication thresholds.

Keep the user prompt unchanged. A controlled local comparison can reuse the
baseline's blueprint with `blueprint_from_job` and `replan_phrases: true`,
while preserving its seed, duration, tempo, first-pass setting and phrase
planning setting. This isolates the change from a different harmonic form
or cast; the model's note output is still nondeterministic.

An explicit `compare_editorial_modes: true` permits a local experimental A/B
to change first-pass versus reviewed writing using a completed source's
blueprint. All other request fields must still match. This override does
not apply to exact MIDI resume, ordinary local requests or Cloud. Record
the mode difference when interpreting the comparison. The existing
`PULSO_AI_JOINT_ONE_GROUP=1` option allows the AI to author all selected
owners together instead of freezing anchor parts before writing companions.

Experimental jobs store `experiment-context.json` with the worker's SHA-256
and the effective `PULSO_AI_` settings. API credentials are not included.

Analyze the exported score with:

```powershell
node tools/analyze-expressive-ensemble.mjs JOB_ID --output build/expressive-report.json
```

The report measures register within the whole work and two-bar windows,
connected melodic intervals, simultaneous notes, recurring rhythmic shapes,
planned speakers and their actual notes, rapid same-pitch attacks, form and
the final note releases. These are listening evidence, not a quality score.
Wide range, more tracks, more notes or fewer repeated rhythms do not by
themselves establish better music. Two-bar windows are measurement bins;
they do not determine the generated phrase length.

Compare complete one-minute works before extending duration. Listen for a
recognizable opening, a developed continuation, voice relationships, a
prepared contrast and an earned ending in the requested musical language.
Retain the original whenever a revision improves numerical breadth but
weakens identity, rhythm or musical consequence.

## Completed phrase ownership

The reviewed joint writer now checks whether each AI-declared speaker
actually sounds in its completed phrase, not merely somewhere in
the entire composition. A repair must not make a promised climax disappear
to clear a harmonic conflict. This check requests AI revision; it never
inserts notes, enforces a density quota or changes pitches. Unfinished
phrases are deferred, and held notes or earlier attacks within the same
phrase permit a rest in the current technical window. First-pass audition
continues to bypass musical review.
Respondent IDs describe relationships, not exact response deadlines: a
reply may occur in a subsequent speaker phrase. They remain advisory
rather than forcing extra attacks into the preceding statement.

The initial one-minute experiment used prompt `musica como bela bartok`,
120 BPM, seed `4203976088674690812`. Reference job
`e930b535-82b8-4a9c-8065-5c46bfd491ca` and expressive alternative
`791b74e9-199c-41b7-b3ad-3bf67ce10d77` share macro form and cast. The latter
has a new AI phrase plan: it is not an exact note-for-note controlled edit.
All 645 notes of that alternative are direct AI output. Its lead spans
29 semitones instead of 22, but this alone does not establish superiority.
Reviewed all-owner job `643e94ec-1109-42d3-a062-f60f1af501f6` exposed the
missing-phrase validation hole; do not treat a lower clash count in its
partial score as a musical win.

## Candidate for listening: coordinated first writing

Job `7aa1959a-0404-4459-9a12-a0ebf886f6b2`, **The Withheld Turn**, is
available at `http://localhost:4193/?job=7aa1959a-0404-4459-9a12-a0ebf886f6b2`.
It preserves B's macro form and phrase plan, uses one coordinated owner
group, expressive instructions, and first-pass audition. It is a new AI
realization, not a deterministic edit or proof that a flag caused every
observed improvement.

- Exactly 60 seconds, eight sounding tracks, 433 direct AI notes; no reused,
  procedurally generated or transformed notes in the provenance report.
- Every declared speaker phrase has notes. The bassoon rests in the opening
  and ending rather than filling all sections indiscriminately.
- Violin attacks by section: 11, 18, 37, 18. This is observed development,
  not a density schedule imposed by code.
- Lead spans 20 semitones, clarinet 23. The lead's widest two-bar span is
  15 semitones, versus 10 in reference A. Wider range alone is not quality.
- The opening lead starts D5-F5-E5. Its last sentence recalls D5-F5-E5,
  approaches via Eb5, and holds D5 from beat 112.75 to 118. All notes end
  by 59 seconds, leaving one second of terminal space in the 60-second MIDI.
- No unterminated notes or invalid sustains. The generic tonal audit still
  reports chromatic and vertical tensions: 26 overlapping semitone/seventh/
  tritone pairs last at least half a beat. None of these measurements proves
  they are stylistically justified. Listen especially to the clarinet's
  chromatic answers; do not label the work flawless from readiness flags.

Listening map: 0:00-0:16 statement, 0:16-0:30 continuation,
0:30-0:46 intensified demand, 0:46-1:00 consequence and release.

The reviewed comparisons `643e94ec-1109-42d3-a062-f60f1af501f6`,
`757aa316-abe8-4809-80c3-9f9808501248` and
`b60cfce7-1220-4d8f-a3c1-60fe859871c4` were deliberately stopped and
retained for diagnosis. They are not completed compositions. The latter
two exposed an over-strict interpretation of response timing; the final
implementation checks timed speaker promises, not an assumed response
deadline absent from the schema. No Cloud, VST deployment or Git commit
was made for this experiment.
