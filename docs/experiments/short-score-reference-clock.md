# Reference clock and explicit releases

Job fbbe4fe7-bd2e-4f49-8374-e9ef3b193a93 returned a completed argument
with a pad reference at beat 113, duration 16, ending at 129. Pitch 50 is valid
in its tonal policy. The old schema independently allowed beat<=127 and
duration<=128, while runtime required their sum<=128. It also failed to restate
the excerpt-local clock in the dedicated Club brief, encouraging song-position
references instead of short local excerpts. This is not a scale failure.

New reference payloads declare excerpt_endpoints_v1 and author beat/end_beat.
The release is bounded directly by the existing reference clock in the schema.
Each phrase/continuation/handoff reference has its own local zero; the ensemble
excerpt shares one zero across its owners. This reference clock is not the song
duration or the final score's arrangement clock. The writer still composes the
complete actual score separately.

The decoder retains both authored endpoints and derives duration=end-start.
It does not choose 128 as a replacement, subtract one beat from the failed note,
shift notes, or clip gates. Invalid, reversed or conflicting endpoints are
rejected atomically with a field path; historical duration references remain
unchanged. Musical choices and all pitches/owners remain authored by the AI.

Tests cover 113..128 yielding duration 15 exactly when that release was authored,
129 being rejected without mutation, all reference containers, idempotence,
reversed/conflicting intervals and legacy compatibility. Runtime still checks
tonal policy and structural references after interval decoding.
