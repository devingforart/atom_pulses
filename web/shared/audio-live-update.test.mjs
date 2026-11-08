import assert from 'node:assert/strict';
import test from 'node:test';
import { ProductionAudio } from './production-audio.mjs';

const midi = lengthBeats => ({ lengthBeats, beatToSeconds: beat => beat });
const note = (startBeat, durationBeats = 1) => ({ startBeat, durationBeats, pitch: 60, velocity: 80 });
const track = (name, notes) => ({ name, instrument: 'keys', notes });

test('live score refresh keeps voices, clock and scheduled attacks; new future notes play once', () => {
  const audio = new ProductionAudio();
  audio.midi = midi(4);
  audio.tracks = [track('A', [note(0, 3), note(.1)])];
  audio.context = { currentTime: 0 };
  audio.origin = 0;
  audio.cursors = [0];
  audio.playing = true;
  const heard = [];
  audio.note = (_, n) => heard.push(n.startBeat);
  const voice = { stop() { assert.fail('sustained voice was stopped'); } };
  audio.voices.add(voice);
  audio.schedule();
  assert.deepEqual(heard, [0, .1]);
  audio.context.currentTime = .05;
  audio.updateSong(midi(8), [track('A', [note(0, 3), note(.1), note(.18), note(4)])]);
  assert.equal(audio.playing, true);
  assert.equal(audio.origin, 0);
  assert.equal(audio.currentPosition(), .05);
  assert.equal(audio.duration, 8);
  assert.equal(audio.voices.has(voice), true);
  assert.deepEqual(heard, [0, .1, .18]);
  audio.schedule();
  assert.deepEqual(heard, [0, .1, .18]);
  audio.context.currentTime = 3.9;
  audio.schedule();
  assert.deepEqual(heard, [0, .1, .18, 4]);
});

test('paused refresh retains position, mute, solo and patch across reordered tracks', () => {
  const audio = new ProductionAudio();
  audio.midi = midi(10);
  audio.tracks = [{ ...track('A', []), patchId: 'chosen' }, track('B', [])];
  audio.position = 3;
  audio.muted.add(0);
  audio.soloed.add(1);
  audio.updateSong(midi(20), [track('B', []), track('A', [])]);
  assert.equal(audio.position, 3);
  assert.equal(audio.playing, false);
  assert.deepEqual([...audio.muted], [1]);
  assert.deepEqual([...audio.soloed], [0]);
  assert.equal(audio.tracks[1].patchId, 'chosen');
});
