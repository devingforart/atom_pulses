import { test } from 'node:test';
import assert from 'node:assert/strict';
import { measureGroove } from '../scripts/audit-club-groove.mjs';

test('groove evidence counts chord attacks once and measures actual recurrence without edits', () => {
  const tracks = [{ name: 'stabs', notes: [0, 4, 8, 12].flatMap(startBeat =>
    [60, 64, 67].map(pitch => ({ startBeat, pitch, durationBeats: .5 }))) },
  { name: 'hook', notes: [{ startBeat: .25, pitch: 60, durationBeats: 1 }] }];
  const before = JSON.stringify(tracks), audit = measureGroove(tracks);
  assert.equal(audit.tracks[0].attacks, 4);
  assert.deepEqual(audit.tracks[0].beatPhaseHistogram, [[0, 4]]);
  assert.equal(audit.tracks[0].cycles[0].mostFrequent[0].appearances, 4);
  assert.equal(audit.closeSamePitchCrossVoiceAttacks.length, 1);
  assert.equal(JSON.stringify(tracks), before);
});
