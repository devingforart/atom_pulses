import test from 'node:test';
import assert from 'node:assert/strict';
import { validateVoicing } from './club-voicing-ab.mjs';

const allowed = new Set([0, 2, 3, 7, 10]);

test('keeps only AI-authored chord tones with a real change of cardinality', () => {
  const notes = validateVoicing([
    { pitch: 62, velocity: 68 }, { pitch: 51, velocity: 65 },
    { pitch: 58, velocity: 64 },
  ], allowed, 11);
  assert.deepEqual(notes.map(note => note.pitch), [51, 58, 62]);
  assert.ok(notes.every(note => note.startBeat === 32 && note.durationBeats === 4));
  assert.throws(() => validateVoicing(Array(11).fill({ pitch: 51, velocity: 60 }), allowed, 11));
});

test('rejects foreign, duplicate or invalid authored notes', () => {
  assert.throws(() => validateVoicing([{ pitch: 53, velocity: 60 }], allowed, 11));
  assert.throws(() => validateVoicing([{ pitch: 51, velocity: 60 },
    { pitch: 51, velocity: 60 }], allowed, 11));
  assert.throws(() => validateVoicing([{ pitch: 51, velocity: 128 }], allowed, 11));
});
