import assert from 'node:assert/strict';
import { test } from 'node:test';
import { referencePrompt } from '../web/local-studio/reference-anchor.mjs';

const source = { behavior: 'club', bpm: 122, allow_chromatic: false,
  duration_seconds: 60 };
const request = { ...source, prompt: 'Club oscuro sin percusión.', duration_seconds: 180 };
const blueprint = { key: 'C minor', instruments: [
  { id: 'lead', source_voice: 'lead' },
  { id: 'moving_bass', source_voice: 'movement_bass' },
  { id: 'chord_pulse', source_voice: 'harmonic_pulse' },
], sections: [{ harmonic_events: [{ chord_id: 'Cm9' }, { chord_id: 'Fm7' }] }] };
const notes = [];
for (const instrument_id of ['lead', 'moving_bass', 'chord_pulse'])
  for (let beat = 0; beat < 8; beat++)
    notes.push({ instrument_id, start_beat: beat, pitch: 72 + beat });

test('local reference is grounded in actual accepted notes and remains within worker limit', () => {
  const prompt = referencePrompt(request, source, blueprint, { notes });
  assert.match(prompt, /C5-Db5-D5-Eb5/);
  assert.match(prompt, /Cm9-Fm7/);
  assert.ok(prompt.length <= 600);
});

test('reference refuses a different tempo or an insufficient MIDI source', () => {
  assert.throws(() => referencePrompt({ ...request, bpm: 123 }, source, blueprint, { notes }), /tempo/);
  assert.throws(() => referencePrompt(request, source, blueprint, { notes: [] }), /notas suficientes/);
});
