# Local Club: vertical support and thematic return

Evidence from 3de26b04-6b38-4049-8730-4020d488e700: foreground F77 over
simultaneous sustained E76 in both high pad and texture; a returned phrase
shifted by .25 beat from absolute beat 288.25; terminal arrival at beat 348
with only four beats remaining. All pitches being in key did not resolve
the listener's ensemble-coherence concern.

The local experimental melodic_statement schema now asks for rhythmic_identity
and vertical_support. These carry reference/variation intent and concrete
support pitches/registers plus intentional tension resolutions. Both reach the
writer via the existing musical arc context. New planning/writing guidance
distinguishes literal returns from rhythmic variation and checks accumulated
sustains against foreground holds rather than just a chord label. It preserves
intentional tensions and syncopation, harmonic continuity and Club pulse.
Closure planning requests perceptible repose relative to phrase pace without
a fixed tail length. No new note transformation, forbidden-interval gate,
automatic rhythm shift or duration quota is introduced. Old plans remain readable.

Tests cover schema presence, planning/writing integration and rejection of an
explicitly blank new declaration. These do not prove musical success.

Next trial: fresh composition, prompt exactly `musica como la de hernan cattaneo`,
180 seconds, 122 BPM, Club, pitched-only, chromaticism off, first pass and phrase
architecture enabled. No reused blueprint/cast/MIDI: this tests the general local
candidate rather than Gravedad Sin Tambor alone. Cloud and prior jobs unchanged.

## Explicit silent terminal lane compatibility

Trial f6383a2e-8041-4914-8462-ab941faa9077 failed during phrase planning,
before MIDI writing. Its last original API response declared plateau_counterline
absent in the final layer state and terminal gesture, with joins_arrival=false
and last_attack_beat=release_beat=0. The terminal validator only understood -1/-1,
whereas layer states conventionally use 0/0. This was a representation mismatch,
not evidence of an invalid musical absence.

Validation now also accepts 0/0 when that same owner has an explicit absent
final-section state and does not join arrival. Active zero-length gestures,
undeclared zero bounds and absent arrival participants remain invalid. No plan
or MIDI is changed. Error messages now include the offending timings and accepted
conventions. Tests cover both representations, active/unknown/joining rejection
and immutability. Retry from the accepted blueprint to preserve macro and cast;
there is no MIDI checkpoint yet, so production planning/writing still run.
