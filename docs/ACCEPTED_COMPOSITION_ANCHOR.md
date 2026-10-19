# Accepted composition anchor

## Current anchor: connected melodic discourse (2026-10-19)

The user accepted **The Pedal's Unanswered Ascent**, job
`6130e724-f8b3-4cd4-b504-b5d0ccb786e4`, after listening to the five-minute
extension of **Bearing Question, Changed Ground**. This supersedes the
previous listening anchor below without deleting it.

- Prompt: `musica como bela bartok`.
- Request: 300 seconds, 120 BPM, adaptive, seed `4203976088674690812`.
- AI-sovereign first pass, phrase architecture and connected-discourse
  delivery planning. No procedural note generation or transformation.
- Result: 304 seconds, 152 bars, 12 populated tracks, 2,329 direct AI notes.
- MIDI SHA-256: `2bd4910920976a7dca22293635e485fddce51ad976f8942b0843b992325eecdb`.
- Reference Windows worker SHA-256:
  `7f7751bd7c63d656b4d4076e262a388aa8f5264df81137e73f0bdcaab51614fd`.
- Frozen listening artifacts: `experiments/anchors/04-connected-discourse-5min/`.
- Shared production profile: `ai-only-v2-connected-discourse` in
  `web/shared/composer-profile.json`. Its AI flags match the accepted job;
  both local and Cloud read that file. Linux and Windows binaries are built
  from the same sources, not expected to have identical binary hashes.

One invalid note exceeded a technical window by one beat; the AI reauthored
that delivery. No note was automatically shortened. All other accepted
windows were preserved. No unterminated notes or invalid sustains remain.
Actual final melodic descent reaches D over D-minor harmony, with final
release at 303.5 seconds and half a second of terminal space.

Articulation ratios remain predominantly connected, but the advisory audit
still flags scalar melodic writing, rhythmic development and chromatic
tensions. Acceptance is the user's listening judgment, not a claim that
all future compositions or all vertical intervals are automatically good.
The API is stochastic; this seed does not guarantee identical new notes.

## Previous anchor (preserved)

The previous accepted local reference was **La Deuda de Re**,
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
