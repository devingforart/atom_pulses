import test from 'node:test';
import assert from 'node:assert/strict';
import { closeNeighborPairs, validatePhrase, overlappingDialogueBars,
  sustainedSemitoneClashes, replaceTracks } from './phrase-dialogue-experiment.mjs';
import { parseMidi, singleTrackMidi } from '../shared/midi.mjs';

test('phrase validator accepts AI-written grid notes without changing their pitches', () => {
  const draft = Array.from({ length: 5 }, (_, i) => ({
    beat: i * 1.5, duration: 0.75, pitch: 69 + i, velocity: 75,
  }));
  const result = validatePhrase(draft, 'PULSO First Violin', 100, 132);
  assert.equal(result[0].startBeat, 100);
  assert.deepEqual(result.map(note => note.pitch), [69, 70, 71, 72, 73]);
  assert.throws(() => validatePhrase([{ ...draft[0], beat: 0.13 }, ...draft.slice(1)],
    'PULSO First Violin', 100, 132), /invalid MIDI event/);
});

test('same-pitch overlap is rejected, but a written repeated note remains possible', () => {
  const notes = Array.from({ length: 5 }, (_, i) => ({
    beat: i, duration: i ? 0.5 : 2, pitch: 69, velocity: 70,
  }));
  assert.throws(() => validatePhrase(notes, 'PULSO First Violin', 100, 132),
    /overlapping same-pitch/);
  notes[0].duration = 0.75;
  assert.equal(validatePhrase(notes, 'PULSO First Violin', 100, 132).length, 5);
});

test('close-neighbour metric measures articulation without rewriting notes', () => {
  const notes = [0, 0.25, 0.5, 2].map((startBeat, i) => ({
    startBeat, pitch: [64, 65, 64, 72][i], durationBeats: 0.25,
  }));
  assert.equal(closeNeighborPairs(notes), 2);
  assert.equal(notes[0].pitch, 64);
});

test('dialogue coverage distinguishes two active voices from a timbral handoff', () => {
  const a = [{ startBeat: 100, durationBeats: 8 },
    { startBeat: 108, durationBeats: 8 }];
  const b = [{ startBeat: 104, durationBeats: 8 }];
  assert.deepEqual(overlappingDialogueBars(a, b, 100, 4), [1, 2]);
  assert.deepEqual(overlappingDialogueBars(a.slice(0, 1),
    [{ startBeat: 108, durationBeats: 8 }], 100, 4), []);
});

test('sustained semitone clash is limited to the inspected bar', () => {
  const violin = [{ startBeat: 170, durationBeats: 1, pitch: 77 }];
  const oboe = [{ startBeat: 170.25, durationBeats: 1, pitch: 76 }];
  assert.equal(sustainedSemitoneClashes(violin, oboe, 168, 172).length, 1);
  assert.equal(sustainedSemitoneClashes(violin, oboe, 172, 176).length, 0);
});

test('MIDI track replacement leaves other track bytes and notes intact', () => {
  const source = singleTrackMidi({ division: 480, beatsPerBar: 4,
    lengthBeats: 8, tempos: [{ tick: 0, microseconds: 500000 }] }, [
    { notes: [{ startBeat: 0, durationBeats: 1, pitch: 60, velocity: 80 }] },
  ]);
  const score = parseMidi(source);
  score.tracks[0].notes = [{ startBeat: 1, durationBeats: 2,
    pitch: 65, velocity: 90, channel: 0 }];
  const unchanged = replaceTracks(source, score, new Set());
  assert.deepEqual(unchanged, source);
  const edited = parseMidi(replaceTracks(source, score, new Set([0])));
  assert.equal(edited.tracks[0].notes[0].pitch, 65);
  assert.equal(edited.tracks[0].notes[0].startBeat, 1);
});
