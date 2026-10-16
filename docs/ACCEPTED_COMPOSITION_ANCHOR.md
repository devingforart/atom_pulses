# Accepted composition anchor

The accepted local reference for the current Cloud composer is **La Deuda de Re**,
job `c438a141-86c2-4712-a356-fbefe428ebe8` (2026-10-16). The user's explicit
job ID distinguishes it from later A/B jobs with the same title.

- Source commit: `251d722` on `stable1`.
- Prompt: `musica como la de Bela Bartok sin baterias ni percusiones. buena narrativa coherente.`
- Request: 390 seconds, 120 BPM, adaptive behavior, seed `4461090869835964068`,
  AI-sovereign first pass with phrase architecture enabled.
- Result: 192 bars, 9 populated MIDI tracks. The local reference file is
  `build/local-studio-data/jobs/c438a141-86c2-4712-a356-fbefe428ebe8/full-song.mid`.
- SHA-256 of that MIDI: `b66ec37c193263174012a1d0f7a88e978afbc940bc78aa27172f3d6bc0d3ff8b`.

The MIDI is a local listening reference, not a deterministic fixture or a
download bundled with Cloud. The same code, prompt and seed need not yield the
same notes on a new model request. Do not confuse this anchor with job
`e28662ef-2361-412c-ac9b-cf0efa1f8a4f`, which reused its blueprint and
rewrote the performance without the long-form writing brief; that A/B variant
was **not** selected for deployment.
