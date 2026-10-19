import test from 'node:test';
import assert from 'node:assert/strict';
import { measureTrackDelivery, analyzeMelodicDelivery } from './melodic-delivery-audit.mjs';

const note = (startBeat, durationBeats, pitch = 60) => ({ startBeat, durationBeats, pitch, velocity: 80 });
test('delivery evidence follows the tempo map without changing notes', () => {
  const midi = { beatToSeconds: beat => beat <= 4 ? beat * .5 : 2 + (beat - 4), beatsPerBar: 4 };
  const track = { notes: [note(0, .25), note(.5, .25), note(1, .25), note(5, .5)] };
  const before = structuredClone(track);
  const audit = measureTrackDelivery(track, midi);
  assert.equal(audit.under200ms, 3);
  assert.equal(audit.rapidRuns.count, 1);
  assert.equal(audit.rapidRuns.examples[0].attacks, 3);
  assert.equal(audit.medianDurationSeconds, .125);
  assert.deepEqual(track, before);
});
test('chord tones are not mistaken for successive melodic attacks', () => {
  const midi = { beatToSeconds: beat => beat * .5 };
  const result = measureTrackDelivery({ notes: [note(0, 1), note(0, 1, 64), note(.25, 1)] }, midi);
  assert.equal(result.monophonicTransitions, 0);
  assert.equal(result.rapidRuns.count, 0);
});
test('a fast connected run remains distinct from clipped articulation', () => {
  const midi = { beatToSeconds: beat => beat * .5 };
  const track = { notes: [note(0, .25), note(.25, .25, 62), note(.5, .25, 64)] };
  const result = measureTrackDelivery(track, midi);
  assert.equal(result.under200ms, 3);
  assert.equal(result.rapidRuns.count, 1);
  assert.equal(result.separatedShortLinks.count, 0);
  assert.equal(result.medianArticulationRatio, 1);
  assert.equal(result.soundingSeconds, .375);
});
test('owner matching preserves a silent track and translates the AI sketch', () => {
  const midi = { beatsPerBar: 4, beatToSeconds: beat => beat * .5,
    tracks: [{ name: 'PULSO Solo', notes: [note(6, 3, 72)] }] };
  const plan = { cast: [{ id: 'silent', name: 'Silent' }, { id: 'solo', name: 'Solo' }],
    sections: [{ start_bar: 0 }] };
  const architecture = { phrases: [{ section_index: 0, local_start_bar: 1, bars: 4,
    speaker_id: 'solo', response_id: '', delivery: {
      structural_tones: [{ beat: 2, duration: 3, pitch: 72, purpose: 'arrival' }] } }] };
  const result = analyzeMelodicDelivery(midi, plan, architecture);
  assert.equal(result.tracks[0].id, 'solo');
  assert.deepEqual(result.phrases[0].bearingTones[0].actualDurations, [3]);
  assert.equal(result.advisoryOnly, true);
});
