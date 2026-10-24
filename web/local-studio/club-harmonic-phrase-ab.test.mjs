import test from 'node:test';
import assert from 'node:assert/strict';
import { validatePhrase } from './club-harmonic-phrase-ab.mjs';

const chords = [[0, 2, 3, 7, 10], [0, 3, 5, 8],
  [0, 3, 7, 8], [2, 5, 7, 10]].map(notes => new Set(notes));
const valid = [
  { beat: 32, duration: 4, pitch: 51, velocity: 60 },
  { beat: 36, duration: 4, pitch: 53, velocity: 60 },
  { beat: 40, duration: 4, pitch: 56, velocity: 60 },
  { beat: 44, duration: 4, pitch: 55, velocity: 60 },
];

test('accepts an AI-written four-bar sustained phrase', () => {
  const notes = validatePhrase(valid, chords, [45, 67]);
  assert.equal(notes.length, 4);
  assert.deepEqual(notes.map(note => note.startBeat), [32, 36, 40, 44]);
});

test('rejects a silent bar and notes outside each chord', () => {
  assert.throws(() => validatePhrase(valid.slice(0, 3), chords, [45, 67]),
    /silent/);
  assert.throws(() => validatePhrase([{ ...valid[0], pitch: 53 },
    ...valid.slice(1)], chords, [45, 67]), /non-chord/);
});

test('rejects hanging, duplicate and unquantized notes', () => {
  assert.throws(() => validatePhrase([{ ...valid[0], duration: 17 },
    ...valid.slice(1)], chords, [45, 67]));
  assert.throws(() => validatePhrase([valid[0], valid[0],
    ...valid.slice(1)], chords, [45, 67]), /overlapping/);
  assert.throws(() => validatePhrase([{ ...valid[0], beat: 32.125 },
    ...valid.slice(1)], chords, [45, 67]), /out-of-bounds/);
});
