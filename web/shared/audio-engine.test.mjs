import assert from 'node:assert/strict';
import { test } from 'node:test';
import { SuiteAudio, startAudioClock } from './audio-engine.mjs';
import { readFileSync } from 'node:fs';

test('background playback clock advances from audio events and is cleaned up on pause', () => {
  const sources = [];
  let ticks = 0;
  const context = {
    sampleRate: 48000, destination: {},
    createBuffer: (channels, length, rate) => ({ channels, length, rate }),
    createBufferSource: () => {
      const source = { connect() {}, disconnect() {}, start() {}, stop() { this.stopped = true; } };
      sources.push(source); return source;
    }
  };
  const stop = startAudioClock(context, () => ++ticks);
  assert.equal(sources[0].buffer.length, 2400);
  sources[0].onended();
  sources[1].onended();
  assert.equal(ticks, 2);
  assert.equal(sources.length, 3);
  stop();
  assert.equal(sources[2].stopped, true);
  assert.equal(sources[2].onended, null);
});

test('audio scheduler finishes transport without any animation frame', () => {
  const original = globalThis.cancelAnimationFrame;
  globalThis.cancelAnimationFrame = () => {};
  try {
    const player = new SuiteAudio();
    player.midi = { beatToSeconds: x => x, lengthBeats: 1 };
    player.context = { currentTime: 1.6 };
    player.origin = 0; player.playing = true; player.playbackTailSeconds = .55;
    let cleaned = false;
    player.stopClock = () => { cleaned = true; };
    player.schedule();
    assert.equal(player.playing, false);
    assert.equal(cleaned, true);
  } finally { globalThis.cancelAnimationFrame = original; }
});

test('local studio never pauses audio on a visibility change', () => {
  const app = readFileSync(new URL('../local-studio/app.js', import.meta.url), 'utf8');
  const listener = app.split('\n').find(line => line.includes("addEventListener('visibilitychange'"));
  assert.ok(listener);
  assert.equal(listener.includes('audio.pause'), false);
  assert.ok(listener.includes('updatePosition(audio.currentPosition())'));
});

test('the transport lets the final piano release finish before stopping', () => {
  const previousAnimationFrame = globalThis.requestAnimationFrame;
  const previousCancelAnimationFrame = globalThis.cancelAnimationFrame;
  globalThis.requestAnimationFrame = () => 1;
  globalThis.cancelAnimationFrame = () => {};
  try {
    let stops = 0;
    let position = -1;
    const player = new SuiteAudio(value => { position = value; }, () => { stops += 1; });
    player.midi = { beatToSeconds: beat => beat, lengthBeats: 1 };
    player.context = { currentTime: 1.2 };
    player.origin = 0;
    player.playing = true;
    player.playbackTailSeconds = .55;
    player.stopVoices = () => {};
    player.animate();
    assert.equal(player.playing, true);
    assert.equal(position, 1);
    assert.equal(stops, 0);

    player.context.currentTime = 1.6;
    player.animate();
    assert.equal(player.playing, false);
    assert.equal(stops, 1);
  } finally {
    globalThis.requestAnimationFrame = previousAnimationFrame;
    globalThis.cancelAnimationFrame = previousCancelAnimationFrame;
  }
});
