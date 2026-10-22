import test from 'node:test';
import assert from 'node:assert/strict';
import { blueprintRequestMatchesSource, exactResumeMatchesCheckpoint, completedMusicalWindows, completedMidiResume } from
  '../web/local-studio/checkpoint-integrity.mjs';

const checkpoint = { resumeFile: 'resume-score.json', completed_units: 3,
  notes: 1, parts: 2 };
const resume = { schema_version: 2, completed_units: 3,
  cast_ids: ['lead', 'bass', 'later_pad'], sections: [{ start_bar: 0, bars: 8 }],
  blueprint_json: '{"title":"Example"}', phrase_architecture_json: '{"phrases":[]}',
  notes: [{ instrument_id: 'lead', start_beat: 0, duration_beats: 1,
    pitch: 60, velocity: 90 }] };

test('completed review counts the same Club technical windows as the writer', () => {
  const score = { ...resume, sections: [{ start_bar: 0, bars: 32 }],
    phrase_architecture_json: '{"kind":"club_production_v1"}' };
  assert.equal(completedMusicalWindows(score, 'club'), 4);
  assert.equal(completedMusicalWindows(score, 'adaptive'), 2);
  assert.equal(completedMusicalWindows({ ...score, phrase_architecture_json: '{"phrases":[]}' }, 'club'), 4);
});

test('exact MIDI resume preserves a complete cast with silent future instruments', () => {
  assert.equal(exactResumeMatchesCheckpoint(checkpoint, resume), true);
  assert.equal(resume.phrase_architecture_json, '{"phrases":[]}');
});

test('completed MIDI retains accepted revisions absent from a stale provisional checkpoint', () => {
  const source = { ...resume, cast_ids: ['pad'], completed_units: 1 };
  const plan = { cast: [{ id: 'pad', name: 'Pad' }] };
  const manifest = { all_midi_notes: 2, composition_behavior: 'adaptive' };
  const midi = { tracks: [{ notes: [] }, { name: 'PULSO Pad', notes: [0, 8].map(startBeat =>
    ({ startBeat, durationBeats: 4, pitch: 60, velocity: 70 })) }] };
  const recovered = completedMidiResume(midi, plan, manifest, source);
  assert.equal(recovered.notes.length, 2);
  assert.equal(recovered.notes[1].start_beat, 8);
  assert.equal(source.notes.length, 1);
  assert.equal(completedMidiResume(midi, plan, { ...manifest, all_midi_notes: 3 }, source), null);
  midi.tracks[1].name = 'Wrong owner';
  assert.equal(completedMidiResume(midi, plan, manifest, source), null);
});

test('exact MIDI resume rejects mismatched or ambiguous checkpoints', () => {
  assert.equal(exactResumeMatchesCheckpoint({ ...checkpoint, notes: 2 }, resume), false);
  assert.equal(exactResumeMatchesCheckpoint(checkpoint,
    { ...resume, cast_ids: ['lead', 'lead', 'later_pad'] }), false);
  assert.equal(exactResumeMatchesCheckpoint(checkpoint,
    { ...resume, cast_ids: ['lead', 'bass'], notes: [{ ...resume.notes[0],
      instrument_id: 'missing' }] }), false);
});

test('blueprint editorial comparisons preserve the musical request and need an explicit override', () => {
  const source = { prompt: 'Chamber work', duration_seconds: 60, bpm: 120,
    behavior: 'adaptive', seed: '1234', ai_sovereign: true,
    phrase_architecture_experiment: true, first_pass_audition: true };
  const review = { ...source, first_pass_audition: false };
  assert.equal(blueprintRequestMatchesSource(source, source), true);
  assert.equal(blueprintRequestMatchesSource(source, review), false);
  assert.equal(blueprintRequestMatchesSource(source, review, true), true);
  for (const changed of [{ bpm: 121 }, { duration_seconds: 90 }, { seed: '5678' },
    { allow_chromatic: true }, { pitched_only: true },
    { prompt: 'Another work' }, { behavior: 'narrative' }, { behavior: 'club' },
    { ai_sovereign: false }, { phrase_architecture_experiment: false }])
    assert.equal(blueprintRequestMatchesSource(source, { ...review, ...changed }, true), false);
});
