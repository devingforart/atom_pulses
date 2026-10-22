import test from 'node:test';
import assert from 'node:assert/strict';
import { blueprintRequestMatchesSource, exactResumeMatchesCheckpoint } from
  '../web/local-studio/checkpoint-integrity.mjs';

const checkpoint = { resumeFile: 'resume-score.json', completed_units: 3,
  notes: 1, parts: 2 };
const resume = { schema_version: 2, completed_units: 3,
  cast_ids: ['lead', 'bass', 'later_pad'], sections: [{ start_bar: 0, bars: 8 }],
  blueprint_json: '{"title":"Example"}', phrase_architecture_json: '{"phrases":[]}',
  notes: [{ instrument_id: 'lead', start_beat: 0, duration_beats: 1,
    pitch: 60, velocity: 90 }] };

test('exact MIDI resume preserves a complete cast with silent future instruments', () => {
  assert.equal(exactResumeMatchesCheckpoint(checkpoint, resume), true);
  assert.equal(resume.phrase_architecture_json, '{"phrases":[]}');
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
    { prompt: 'Another work' }, { behavior: 'narrative' },
    { ai_sovereign: false }, { phrase_architecture_experiment: false }])
    assert.equal(blueprintRequestMatchesSource(source, { ...review, ...changed }, true), false);
});
