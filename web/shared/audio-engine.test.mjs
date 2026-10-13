import assert from 'node:assert/strict';
import { test } from 'node:test';
import { SuiteAudio } from './audio-engine.mjs';

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
