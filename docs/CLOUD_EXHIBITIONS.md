# Cloud exhibitions and pitch controls

This change starts from deployed Cloud revision `78fdaf2` on branch `1.0`.
It does not enable the discarded local composition experiments.

## Composition

- Local Studio and Cloud exclude drum/percussion owners through the shared
  composer profile. Tonal rhythm, stabs, arpeggios and bass remain available.
- **Permitir cromatismos** is off by default. The boolean is persisted with
  each Cloud job and carried through recovery, never taken from another job.
- Off means all pitched notes must belong to the declared scale. On permits,
  but does not require, purposeful external notes.
- Requests and structured schemas express these restrictions to the AI.
  Validation does not retune, remove or add musical notes.
- Existing saved MIDI files are not modified.

## Sharing

In Cloud, select a completed work and click **Share**. The dialog provides a
public `/share/<token>` link, copying, opening, and revocation for all links
issued for that work. The recipient needs no account or composition credits.

The exhibition displays the arrangement and playback only. Public API responses
include basic score metadata and per-track MIDI, never account details, prompts,
job logs, composition plans, API keys or library access. MIDI is necessarily
transferred to the recipient for browser playback; this is not download DRM.

Only an authenticated owner can issue or revoke links. Tokens contain 256 bits
of randomness and only their SHA-256 hashes are persisted. Unshared, unfinished,
revoked and missing works are unavailable. Public MIDI paths are allowlisted
against the completed manifest; traversal and non-MIDI files are rejected.
Responses are not cached so revocation applies to subsequent network requests.
Already received files cannot be recalled from a recipient's device.

Local Studio retains its local-only `/?share=<job-id>` viewer. It does not upload
local works or publish them automatically. Public links are issued in Cloud.

## Operations

Migration `0007_cloud_exhibitions.sql` adds the pitch-policy column and exhibition
table without changing existing compositions. Deploy only when Cloud has no
queued/running jobs. Use `scripts/deploy-cloud-release.sh` with an exact commit;
it checks jobs before activation and preserves the previous image for rollback.

Verification: frontend typecheck/tests/build, Rust tests/clippy, C++ pipeline
tests, local viewer tests, and anonymous production health/access checks.
