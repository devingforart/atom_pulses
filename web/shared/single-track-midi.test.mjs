import assert from 'node:assert/strict';
import { test } from 'node:test';
import { parseMidi, singleTrackMidi } from './midi.mjs';

test('one-track export preserves every authored onset, pitch, velocity, duration and tempo', () => {
  const score = { division: 960, beatsPerBar: 4, lengthBeats: 8,
    tempos: [{ tick: 0, microseconds: 600000 }, { tick: 3840, microseconds: 500000 }] };
  const tracks = [
    { notes: [{ startBeat: 0, durationBeats: 2, pitch: 60, velocity: 90, channel: 0 },
      { startBeat: 4, durationBeats: 1, pitch: 64, velocity: 82, channel: 1 }] },
    { notes: [{ startBeat: 0, durationBeats: .5, pitch: 36, velocity: 110, channel: 9 },
      { startBeat: 4.25, durationBeats: 1.25, pitch: 67, velocity: 70, channel: 2 }] },
  ];
  const data = singleTrackMidi(score, tracks);
  const parsed = parseMidi(data);
  assert.equal(parsed.format, 0);
  assert.equal(parsed.tracks.length, 1);
  assert.equal(parsed.tracks[0].name, 'PULSO - Todas las notas');
  assert.equal(parsed.tracks[0].notes.length, 4);
  assert.equal(parsed.lengthBeats, 8);
  assert.equal(parsed.beatToSeconds(8), 4 * .6 + 4 * .5);
  assert.deepEqual(parsed.tracks[0].notes.map(note => [note.startBeat, note.durationBeats, note.pitch, note.velocity, note.channel]),
    [[0, 2, 60, 90, 0], [0, .5, 36, 110, 0], [4, 1, 64, 82, 0], [4.25, 1.25, 67, 70, 0]].sort((a, b) => a[0] - b[0] || a[2] - b[2]));
});
