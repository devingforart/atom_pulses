# iPhone audition compatibility

User report: transport advances but both piano and neutral audition are silent
on iPhone, while desktop and Spotify work. No physical iPhone was available to
reproduce its output route, so the underlying device-specific cause is not
claimed as conclusively observed.

The shared player now requests `navigator.audioSession.type = 'playback'` on
the explicit Play gesture, before context creation/resume or any asynchronous
sample fetch. The Audio Session specification defines AudioContext's default
type as ambient, whereas playback is intended for music. Unsupported/throwing
Audio Session implementations cannot block desktop playback. Offline exports
and score loading do not claim a music session.

Context resume is synchronous in that gesture's call stack, bounded and checked:
an unresolved or still-interrupted context cannot start a falsely playing
transport. Actual OS state interruptions pause without resetting position and
update the Play button; the next user Play resumes from that position. Merely
switching browser tabs does not pause a running context. This does not promise
background playback where iOS itself suspends Web Audio.

The same CC0 piano samples have MP3 renditions for Safari versions without OGG
Vorbis support. Codec capability selects the first format; actual decoding
failure tries the other. Both sets are shipped from the same origin. MIDI
pitches, timing, notes, velocities and exports are unchanged.

Checks: shared player unit tests (gesture ordering, optional APIs, interruption,
bounded resume, codec fallback and asset completeness), existing scheduling,
live-update, unison and WAV tests; React playback UI tests and production build.
Real-device confirmation remains necessary after a fresh page load on iPhone.
The optional `tools/check-web-audio-playback.mjs` smoke test also renders actual
Chrome graphs for neutral and MP3 piano playback and verifies nonzero PCM. It
uses a silent sink because the headless environment has no speaker device; it
is not a physical iPhone or audibility test.

Deploy with `scripts/deploy-web-audio.sh` from a byte-preserving Git archive.
It builds/tests only the frontend, atomically replaces the public static site,
checks API/Cloud/MP3 availability and supports rollback. It neither restarts the
API nor cancels compositions, changes database state or modifies engine 1.2.

References:
- https://www.w3.org/TR/audio-session/#audiocontext
- https://www.w3.org/TR/audio-session/#audio-session-types
- https://webkit.org/blog/16574/webkit-features-in-safari-18-4/
