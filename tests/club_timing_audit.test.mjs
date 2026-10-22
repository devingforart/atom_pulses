import test from 'node:test';
import assert from 'node:assert/strict';
import { summarizeTiming } from '../scripts/audit-club-timing.mjs';

test('checks authored cycle phase without moving notes', () => {
  const notes = [{ startBeat: 0 }, { startBeat: 8.25 }, { startBeat: 16 }];
  const before = JSON.stringify(notes);
  const layer = { timing_mode: 'cycle_bound', cycle_beats: 8, cycle_origin_beat: 0, rhythm_cell: [0] };
  assert.equal(summarizeTiming(notes, layer).contractMismatches, 1);
  assert.equal(JSON.stringify(notes), before);
  assert.equal(summarizeTiming(notes, { ...layer, timing_mode: 'phrase_flexible' }).contractMismatches, 0);
});

test('intentional sixteenth syncopation remains valid when declared', () => {
  const layer = { timing_mode: 'cycle_bound', cycle_beats: 8, cycle_origin_beat: 0, rhythm_cell: [.25] };
  const audit = summarizeTiming([{ startBeat: .25 }, { startBeat: 8.25 }], layer);
  assert.equal(audit.contractMismatches, 0);
  assert.equal(audit.positions.sixteenth, 2);
});
