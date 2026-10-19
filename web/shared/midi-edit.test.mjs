import test from 'node:test';
import assert from 'node:assert/strict';
import { parseMidi } from './midi.mjs';
import { changeMidiNoteDurations } from './midi-edit.mjs';

const chunk = events => [0x4d, 0x54, 0x72, 0x6b, 0, 0, 0, events.length, ...events];
const fixture = Uint8Array.from([
  0x4d, 0x54, 0x68, 0x64, 0, 0, 0, 6, 0, 1, 0, 2, 1, 0xe0,
  ...chunk([0, 0xff, 0x51, 3, 7, 0xa1, 0x20, 0, 0xff, 0x2f, 0]),
  ...chunk([0, 0xc1, 17, 0, 0xb1, 11, 99, 0, 0x91, 76, 80,
    0x81, 0x70, 0x81, 76, 0, 0, 0xff, 0x2f, 0]),
]);

test('edits only a note-off and retains tempo, program, expression and track bytes', () => {
  const edited = changeMidiNoteDurations(fixture,
    [{ trackIndex: 1, noteIndex: 0, durationBeats: 1 }]);
  const midi = parseMidi(edited);
  assert.equal(midi.tracks[1].notes[0].durationBeats, 1);
  assert.equal(midi.tempos.at(-1).microseconds, 500000);
  assert.equal(parseMidi(fixture).tracks[1].notes[0].durationBeats, .5);
  assert.deepEqual([...edited.slice(14, 32)], [...fixture.slice(14, 32)]);
  assert.equal(Buffer.from(edited).includes(Buffer.from([0xc1, 17, 0, 0xb1, 11, 99])), true);
});

test('invalid and duplicate note edits fail closed', () => {
  assert.throws(() => changeMidiNoteDurations(fixture,
    [{ trackIndex: 1, noteIndex: 1, durationBeats: 1 }]));
  assert.throws(() => changeMidiNoteDurations(fixture, [
    { trackIndex: 1, noteIndex: 0, durationBeats: 1 },
    { trackIndex: 1, noteIndex: 0, durationBeats: 2 },
  ]));
});
