import test from 'node:test';
import assert from 'node:assert/strict';
import { phraseResponseGaps, repeatedFourNoteCells,
  sustainedSemitoneAgainstHarmony, validateAuthoredVoice } from './narrative-editor-ab.mjs';

const event = (beat, pitch, duration = 0.5) =>
  ({ beat, pitch, duration, velocity: 80 });

test('validates AI-authored notes without changing their timing or pitch', () => {
  const result = validateAuthoredVoice([event(1, 74), event(0, 72)],
    'lead', 0, 4, [60, 90]);
  assert.deepEqual(result.map(note => [note.startBeat, note.pitch]), [[0, 72], [1, 74]]);
  assert.throws(() => validateAuthoredVoice([event(0.1, 72)], 'lead', 0, 4,
    [60, 90]), /invalid authored/);
  assert.throws(() => validateAuthoredVoice([event(0, 72, 2), event(1, 74)],
    'lead', 0, 4, [60, 90]), /overlapping/);
});

test('measures repeated transposed four-note cells', () => {
  const first = [event(0, 72), event(0.5, 74), event(1, 72, 1), event(2, 69)];
  const second = first.map(note => ({ ...note, beat: note.beat + 4,
    pitch: note.pitch + 2 }));
  const notes = [...first, ...second].map(note => ({ startBeat: note.beat,
    durationBeats: note.duration, pitch: note.pitch }));
  assert.equal(repeatedFourNoteCells(notes), 2);
});

test('detects planned answer windows without literal note quotas', () => {
  const plan = { beats_per_bar: 4 };
  const audit = { phrases: [{ absoluteStartBar: 2, bars: 4,
    responseId: 'clarinet' }, { absoluteStartBar: 8, bars: 3,
    responseId: 'clarinet' }] };
  assert.equal(phraseResponseGaps(plan, audit, [{ startBeat: 9 }], 'clarinet'), 1);
});

test('measures prolonged chromatic friction without rewriting it', () => {
  const lead = [{ startBeat: 2, durationBeats: 1, pitch: 90 }];
  const piano = [{ startBeat: 0, durationBeats: 4, pitch: 65 }];
  assert.equal(sustainedSemitoneAgainstHarmony(lead, piano, 0, 4), 1);
});
