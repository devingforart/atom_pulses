import test from 'node:test';
import assert from 'node:assert/strict';
import { lowRegisterPairs } from '../tools/audit-local-jobs.mjs';

const note = (pitch, startBeat, durationBeats) => ({ pitch, startBeat, durationBeats });
const parts = [
  { part_id: 1, source_voice: 'harmonic_foundation' },
  { part_id: 2, source_voice: 'movement_bass' },
];

test('offline audit locates the two sustained low conflicts from the saved proof', () => {
  const tracks = [
    { notes: [] },
    { notes: [note(58, 4, 6), note(58, 40, 6)] },
    { notes: [note(45, 7, 2), note(45, 43, 3)] },
  ];
  assert.deepEqual(lowRegisterPairs(tracks, parts).map(pair =>
    [pair.beat, pair.bassPitch, pair.padPitch, pair.overlapBeats]),
  [[7, 45, 58, 2], [43, 45, 58, 3]]);
});

test('a spacious upper colour is not labelled as low-register mud', () => {
  const tracks = [
    { notes: [] },
    { notes: [note(76, 0, 4)] },
    { notes: [note(38, 0, 4)] },
  ];
  assert.equal(lowRegisterPairs(tracks, parts).length, 0);
});
