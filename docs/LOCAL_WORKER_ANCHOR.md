# Accepted local worker anchor — 2026-10-21

The user accepted the latest completed generation and requested a commit/push,
without deploying. This is the local development baseline to preserve.

## Accepted configuration

- Entry point: `node web/local-studio/start-gesture-score-sol.mjs`.
- Local studio: `http://localhost:4212`.
- Model: `gpt-6.1-sol`, medium reasoning (confirmed in the actual API trace).
- Writing: AI-authored performed gestures across the complete reduced score,
  followed by one explicit scoped AI rehearsal and accompaniment against the
  immutable selected notes. No procedural musical expansion or ornament insertion.
- Audition: full arrangement with piano, not the argument-only reduction.
- No shared production model/profile change, release or deployment is implied.

Build the worker if necessary:

```powershell
cmake -S . -B build-development -DPULSO_BUILD_PLUGIN=OFF -DPULSO_BUILD_CLOUD_WORKER=ON -DPULSO_BUILD_TESTS=ON
cmake --build build-development --config Release --target pulso_cloud_worker
node web/local-studio/start-gesture-score-sol.mjs
```

The executable is a generated artifact and is not committed. Private API
credentials remain external to Git.

## Accepted musical reference

- Job: `8972f814-030b-447d-8e36-8519b5cd9551`.
- Title: `The D Question, Answered in G`.
- Prompt: `Musica como la de mozart.`
- Seed: `202610200201`; duration 60 seconds, tempo 120 BPM, G major.
- Eight sounding tracks, 561 direct-AI notes; no procedural note transformations.
- The first AI argument was kept. Its 315 notes survived orchestration unchanged.
- User listening acceptance on 2026-10-21 is the anchor, not an automatic score.

A durable copy of the reference and its composition data is stored in
`experiments/anchors/gesture-score-sol-2026-10-21/`. This preserves the accepted
MIDI even if the temporary studio history is cleared. Its historical request
references a local source UUID; it is provenance, not a standalone replay command.
API output is not guaranteed identical for the same prompt/seed. The saved notes
are the exact listening reference; the launcher preserves the generator profile.

See `gesture-score-study.md` for the controlled comparisons and limitations.
Earlier experimental launchers remain research references, not the selected
development configuration. Do not silently return the local anchor to Terra or
promote it to Cloud without the user's next instruction.
