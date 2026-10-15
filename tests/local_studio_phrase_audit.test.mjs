import test from 'node:test';
import assert from 'node:assert/strict';
import { analyzePhraseArchitecture } from '../web/local-studio/phrase-audit.mjs';

test('phrase audit reports actual MIDI evidence without calling it musical quality', () => {
  const architecture = { phrases: [
    { section_index: 0, local_start_bar: 0, bars: 2, speaker_id: 'lead',
      response_id: 'bass', register_low: 70, register_high: 85 },
    { section_index: 1, local_start_bar: 0, bars: 2, speaker_id: 'lead',
      response_id: '', register_low: 70, register_high: 85 },
  ] };
  const plan = { cast: [{ id: 'lead' }, { id: 'bass' }],
    sections: [{ start_bar: 0 }, { start_bar: 2 }] };
  const midi = { beatsPerBar: 4, tracks: [
    { notes: [] },
    { notes: [{ startBeat: 1, pitch: 72 }, { startBeat: 3, pitch: 90 }] },
    { notes: [{ startBeat: 4, pitch: 40 }] },
  ] };
  const result = analyzePhraseArchitecture(architecture, plan, midi);
  assert.equal(result.advisoryOnly, true);
  assert.equal(result.phrases[0].speakerNotes, 2);
  assert.equal(result.phrases[0].responseNotes, 1);
  assert.equal(result.phrases[0].registerAdherence, 0.5);
  assert.equal(result.phrases[0].needsListening, true);
  assert.equal(result.silentSpeakerPhrases, 1);
});
