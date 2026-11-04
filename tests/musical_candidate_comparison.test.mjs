import test from 'node:test';
import assert from 'node:assert/strict';
import {delta,observations} from '../scripts/compare-musical-candidates.mjs';
test('MIDI comparison preserves duplicate events and detects real revisions',()=>{
  const a={startBeat:0,durationBeats:1,pitch:60,velocity:90,channel:0};
  assert.deepEqual(delta([a,a],[a]),{retained:1,removed:1,added:0});
  assert.deepEqual(delta([a],[{...a,pitch:62}]),{retained:0,removed:1,added:1});
  assert.deepEqual(delta([a],[a]),{retained:1,removed:0,added:0});
});
test('observations distinguish polyphony from rhythmic activity without grading music',()=>{
  const notes=[60,64,67].map(pitch=>({startBeat:0,durationBeats:4,pitch,velocity:90,channel:0}));
  const snapshot=JSON.stringify(notes), stats=observations(notes);
  assert.equal(stats.noteCount,3);assert.equal(stats.distinctAttackTimes,1);
  assert.equal(stats.medianDurationBeats,4);assert.equal(stats.medianOnsetGapBeats,null);
  assert.deepEqual(stats.pitchRange,[60,67]);assert.equal(JSON.stringify(notes),snapshot);
  assert.equal(observations([]).medianDurationBeats,null);
  assert.throws(()=>observations([],0));
});
