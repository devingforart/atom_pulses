import test from 'node:test';
import assert from 'node:assert/strict';
import { foregroundTrackIndex, validateArticulationChanges } from './articulation-review-ab.mjs';

const score = { lengthBeats: 16, tracks: [
  { name: 'tempo', notes: [] },
  { name: 'pad', notes: [{ startBeat: 0, durationBeats: 16, pitch: 60 }] },
  { name: 'lead', notes: [
    { startBeat: 0, durationBeats: .5, pitch: 64, velocity: 80, channel: 1 },
    { startBeat: 2, durationBeats: .5, pitch: 67, velocity: 82, channel: 1 },
  ] },
] };

test('AI articulation accepts a selective release without changing note identity', () => {
  assert.equal(foregroundTrackIndex({ tracks: [{}, {}] },
    { protagonist_id: 'lead', cast: [{ id: 'pad' }, { id: 'lead' }] }), 2);
  const edits = validateArticulationChanges({ changes: [
    { note_index: 0, duration_beats: 1.5, reason: 'Hold the first phrase destination' },
  ] }, score, 2);
  assert.equal(edits[0].durationBeats, 1.5);
  assert.equal(edits[0].pitch, 64);
});

test('AI articulation rejects note addition, adjacent overlap and new semitone clash', () => {
  assert.throws(() => validateArticulationChanges({ changes: [
    { note_index: 7, duration_beats: 1, reason: '' },
  ] }, score, 2));
  assert.throws(() => validateArticulationChanges({ changes: [
    { note_index: 0, duration_beats: 2.25, reason: '' },
  ] }, score, 2));
  const clash = structuredClone(score);
  clash.tracks[1].notes[0].pitch = 65;
  assert.throws(() => validateArticulationChanges({ changes: [
    { note_index: 0, duration_beats: 1.5, reason: '' },
  ] }, clash, 2));
});
