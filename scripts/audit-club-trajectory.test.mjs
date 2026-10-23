import test from 'node:test';
import assert from 'node:assert/strict';
import { trajectoryEvidence } from './audit-club-trajectory.mjs';

test('activity evidence uses section-local bounds and leaves notes unchanged', () => {
  const plan = { beats_per_bar: 4, total_bars: 4, cast: [{ id: 'pad', name: 'Pad' }],
    sections: [{ start_bar: 0, bars: 2 }, { start_bar: 2, bars: 2 }] };
  const production = { layers: [{ instrument_id: 'pad', states: [
    { section_index: 0, mode: 'absent', entry_beat: 0, release_beat: 0 },
    { section_index: 1, mode: 'active', entry_beat: 2, release_beat: 6 }] }] };
  const tracks = [{ name: 'Pad', notes: [{ startBeat: 10, durationBeats: 4, pitch: 60, velocity: 70 }] }];
  const before = JSON.stringify(tracks);
  assert.equal(trajectoryEvidence(plan, production, tracks).activityContractViolations, 0);
  assert.equal(JSON.stringify(tracks), before);
  tracks[0].notes[0].durationBeats = 5;
  assert.equal(trajectoryEvidence(plan, production, tracks).activityContractViolations, 1);
  tracks[0].notes[0].startBeat = 0;
  assert.equal(trajectoryEvidence(plan, production, tracks).activityContractViolations, 1);
});

test('legacy plans keep complete section scope without inventing a withdrawal', () => {
  const plan = { beats_per_bar: 4, total_bars: 2, cast: [{ id: 'lead', name: 'Lead' }],
    sections: [{ start_bar: 0, bars: 2 }] };
  const production = { layers: [{ instrument_id: 'lead', states: [{ section_index: 0, mode: 'active' }] }] };
  const result = trajectoryEvidence(plan, production, [{ name: 'Lead', notes: [
    { startBeat: 0, durationBeats: 8, pitch: 60, velocity: 90 }] }]);
  assert.equal(result.activityContractViolations, 0);
  assert.equal(result.voicesAtFirstInstant, 1);
  assert.equal(result.voicesInFinalBar, 1);
});
