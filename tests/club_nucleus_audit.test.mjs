import test from 'node:test';
import assert from 'node:assert/strict';
import { measureLane } from '../scripts/audit-club-nucleus.mjs';

test('nucleus audit measures gaps and recurrence without modifying notes', () => {
  const notes = [{ startBeat: 0, durationBeats: 2, pitch: 60 },
    { startBeat: 0, durationBeats: 2, pitch: 64 },
    { startBeat: 4, durationBeats: 4, pitch: 60 }];
  const before = JSON.stringify(notes);
  const result = measureLane(notes, [{ section_index: 0, mode: 'active', entry_beat: 0, release_beat: 8 }],
    [{ start_bar: 0, bars: 2 }], 4, [0, 2]);
  assert.equal(result.activeSoundingFraction, .75);
  assert.equal(result.largestActiveSilenceBeats, 2);
  assert.equal(result.meanReferenceAttackRetention, .5);
  assert.equal(result.distinctAttacks, 2);
  assert.equal(JSON.stringify(notes), before);
});

test('intended absence is not reported as failed sounding coverage', () => {
  const result = measureLane([], [{ section_index: 0, mode: 'absent', entry_beat: 0, release_beat: 0 }],
    [{ start_bar: 0, bars: 8 }], 4, [0]);
  assert.equal(result.activeSoundingFraction, null);
  assert.equal(result.measuredCompleteCycles, 0);
});
