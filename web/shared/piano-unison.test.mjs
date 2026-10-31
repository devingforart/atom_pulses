import assert from 'node:assert/strict';
import { test } from 'node:test';
import { ProductionAudio } from './production-audio.mjs';

function player(mode = 'piano') {
  const audio = new ProductionAudio();
  audio.mode = mode;
  audio.midi = { beatToSeconds: beat => beat, lengthBeats: 8 };
  audio.context = { currentTime: 0 };
  audio.origin = 0;
  audio.playing = true;
  audio.tracks = [
    { instrument: 'lead', notes: [{ startBeat: 0, durationBeats: 1, pitch: 69, velocity: 100 }] },
    { instrument: 'pad', notes: [{ startBeat: 0, durationBeats: 3, pitch: 69, velocity: 70 }] },
  ];
  audio.cursors = [0, 0];
  const heard = [];
  audio.note = (instrument, note, at, seconds) => heard.push({ instrument, note, at, seconds });
  return { audio, heard };
}

test('piano merges exact unisons with strongest velocity and longest release, leaving MIDI intact', () => {
  const { audio, heard } = player();
  const original = JSON.stringify(audio.tracks);
  audio.schedule();
  assert.equal(heard.length, 1);
  assert.equal(heard[0].seconds, 3);
  assert.equal(heard[0].note.velocity, 100);
  assert.equal(JSON.stringify(audio.tracks), original);
  audio.schedule();
  assert.equal(heard.length, 1);
});

test('nearby attacks and distinct pitches remain independent', () => {
  const { audio, heard } = player();
  audio.tracks[1].notes[0].startBeat = .001;
  audio.tracks.push({ instrument: 'lead', notes: [{ startBeat: 0, durationBeats: 1, pitch: 70, velocity: 80 }] });
  audio.schedule();
  assert.equal(heard.length, 3);
});

test('mute and solo are applied before merging', () => {
  for (const solo of [false, true]) {
    const { audio, heard } = player();
    if (solo) audio.soloed.add(1);
    else audio.muted.add(0);
    audio.schedule();
    assert.equal(heard.length, 1);
    assert.equal(heard[0].note.velocity, 70);
    assert.equal(heard[0].seconds, 3);
  }
});

test('seeking merges only voices that originally shared the same attack', () => {
  const { audio, heard } = player();
  audio.position = .5;
  audio.scheduleHeldNotes();
  assert.equal(heard.length, 1);
  assert.equal(heard[0].seconds, 2.5);
  heard.length = 0;
  audio.tracks[1].notes[0].startBeat = .25;
  audio.scheduleHeldNotes();
  assert.equal(heard.length, 2);
});

test('production and sine audition keep their independent voices', () => {
  for (const mode of ['production', 'neutral']) {
    const { audio, heard } = player(mode);
    audio.schedule();
    assert.equal(heard.length, 2);
  }
});
