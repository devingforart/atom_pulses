import { test } from 'node:test';
import assert from 'node:assert/strict';
import { analyzePhraseArchitecture } from '../web/local-studio/phrase-audit.mjs';

test('Club production audit measures AI promises from actual notes without speaker fiction or MIDI edits', () => {
  const architecture = { kind: 'club_production_v1', format: 'continuous_section',
    layers: [{ instrument_id: 'pad', production_role: 'chord body', cycle_beats: 16,
      polyphonic: true, states: [{ section_index: 0, mode: 'sustain', minimum_sounding_fraction: .9 }] }] };
  const plan = { cast: [{ id: 'pad' }], sections: [{ start_bar: 0, bars: 4 }] };
  const midi = { beatsPerBar: 4, tracks: [{ notes: [] }, { notes: [60, 64, 67].map(pitch =>
    ({ startBeat: 0, durationBeats: 16, pitch, velocity: 80 })) }] };
  const before = JSON.stringify(midi);
  let audit = analyzePhraseArchitecture(architecture, plan, midi);
  assert.equal(audit.layers[0].states[0].actualSoundingFraction, 1);
  assert.equal(audit.layers[0].states[0].observedPolyphony, true);
  assert.deepEqual(audit.issues, []);
  assert.equal(JSON.stringify(midi), before);
  midi.tracks[1].notes = [{ startBeat: 0, durationBeats: 1, pitch: 60, velocity: 80 }];
  audit = analyzePhraseArchitecture(architecture, plan, midi);
  assert.deepEqual(audit.issues.map(issue => issue.kind), ['under_declared_coverage', 'missing_declared_polyphony']);
  architecture.layers[0].states[0].mode = 'absent';
  assert.equal(analyzePhraseArchitecture(architecture, plan, midi).issues[0].kind, 'unexpected_activity');
});

test('Club audit exposes core phase drift even for perfectly quantized notes', () => {
  const architecture = { kind: 'club_production_v1', groove_core_ids: ['pulse'],
    layers: [{ instrument_id: 'pulse', cycle_beats: 8, cycle_origin_beat: 0,
      rhythm_cell: [0, 2, 4, 6], states: [] }] };
  const plan = { cast: [{ id: 'pulse' }], sections: [{ start_bar: 0, bars: 16 }] };
  const midi = { beatsPerBar: 4, tracks: [{ notes: [] }, { notes:
    [0, 2, 32.5, 34.5].map(startBeat => ({ startBeat, durationBeats: .5, pitch: 60 })) }] };
  const before = JSON.stringify(midi);
  const audit = analyzePhraseArchitecture(architecture, plan, midi);
  assert.deepEqual(audit.layers[0].phaseMismatchAttacks, [32.5, 34.5]);
  assert.equal(audit.issues[0].kind, 'core_phase_mismatch');
  assert.equal(JSON.stringify(midi), before);
});
