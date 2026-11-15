import assert from 'node:assert/strict';
import test from 'node:test';
import { SuiteAudio, requestPlaybackSession, resumeAudioContext } from './audio-engine.mjs';
import { pianoFormats, loadPianoSample } from './production-audio.mjs';
import { existsSync } from 'node:fs';

test('music playback category is optional and unsupported APIs cannot break desktop audio', () => {
  const session = { type: 'auto' };
  assert.equal(requestPlaybackSession({ audioSession: session }), true);
  assert.equal(session.type, 'playback');
  assert.equal(requestPlaybackSession({}), false);
  assert.equal(requestPlaybackSession({ get audioSession() { throw new Error('unavailable'); } }), false);
});

test('resume is invoked synchronously; interrupted/stuck audio is not reported as playing', async () => {
  let called = false;
  const context = { state: 'interrupted', resume() { called = true; this.state = 'running'; return Promise.resolve(); } };
  const ready = resumeAudioContext(context);
  assert.equal(called, true);
  await ready;
  await assert.rejects(resumeAudioContext({ state: 'interrupted', resume: async () => {} }), /Volvé a tocar/);
  await assert.rejects(resumeAudioContext({ state: 'suspended', resume: () => new Promise(() => {}) }, 10), /Volvé a tocar/);
});

test('Play requests playback before context creation; OS interruptions retain the musical position', async () => {
  const globals = ['navigator', 'AudioContext', 'requestAnimationFrame', 'cancelAnimationFrame'];
  const saved = globals.map(key => [key, Object.getOwnPropertyDescriptor(globalThis, key)]);
  const order = [], listeners = new Map();
  let stopped = 0;
  class FakeContext {
    constructor() { order.push('context'); this.state = 'suspended'; this.currentTime = 0; this.sampleRate = 48000; this.destination = {}; }
    createGain() { return { gain: {}, connect() { return this; } }; }
    createDynamicsCompressor() { return { threshold: {}, knee: {}, ratio: {}, attack: {}, release: {}, connect() { return this; } }; }
    addEventListener(name, callback) { listeners.set(name, callback); }
    resume() { order.push('resume'); this.state = 'running'; return Promise.resolve(); }
    createBuffer() { return {}; }
    createBufferSource() { return { connect() {}, disconnect() {}, start() {}, stop() {} }; }
  }
  try {
    Object.defineProperty(globalThis, 'navigator', { configurable: true, value: { audioSession: {
      set type(value) { order.push(value); }, get type() { return 'playback'; }
    } } });
    Object.defineProperty(globalThis, 'AudioContext', { configurable: true, value: FakeContext });
    Object.defineProperty(globalThis, 'requestAnimationFrame', { configurable: true, value: () => 1 });
    Object.defineProperty(globalThis, 'cancelAnimationFrame', { configurable: true, value: () => {} });
    const player = new SuiteAudio(undefined, () => stopped++);
    player.setSong({ lengthBeats: 10, beatToSeconds: n => n }, [{ name: 'lead', instrument: 'lead', notes: [] }]);
    stopped = 0;
    const playing = player.play();
    assert.deepEqual(order.slice(0, 3), ['playback', 'context', 'resume']);
    await playing;
    assert.equal(player.playing, true);
    player.context.currentTime = 2;
    player.context.state = 'interrupted';
    listeners.get('statechange')();
    assert.equal(player.playing, false);
    assert.equal(player.position, 2);
    assert.equal(stopped, 1);
    await player.play();
    assert.equal(player.playing, true);
    assert.equal(player.currentPosition(), 2);
    player.stop();
  } finally {
    for (const [key, descriptor] of saved) {
      if (descriptor) Object.defineProperty(globalThis, key, descriptor);
      else delete globalThis[key];
    }
  }
});

test('Safari without Vorbis gets the MP3 rendition of the same piano', () => {
  assert.deepEqual(pianoFormats({ createElement: () => ({ canPlayType: () => '' }) }), ['mp3', 'ogg']);
  assert.deepEqual(pianoFormats({ createElement: () => ({ canPlayType: () => 'probably' }) }), ['ogg', 'mp3']);
  for (const name of ['A0', ...Array.from({ length: 8 }, (_, n) => `C${n + 1}`), ...Array.from({ length: 7 }, (_, n) => `Fs${n + 1}`)]) {
    assert.ok(existsSync(new URL(`../frontend/public/piano/INSPiano_Vel03_${name}_01.mp3`, import.meta.url)));
  }
});

test('actual piano decoding failure tries the alternate codec, not a fake buffer', async () => {
  const previous = globalThis.fetch;
  const requests = [];
  try {
    globalThis.fetch = async url => { requests.push(url); return { ok: true, arrayBuffer: async () => new ArrayBuffer(1) }; };
    let attempts = 0;
    const expected = { sample: 'decoded piano' };
    const buffer = await loadPianoSample({ decodeAudioData: async () => {
      if (++attempts === 1) throw new Error('codec not supported');
      return expected;
    } }, 60, ['ogg', 'mp3']);
    assert.equal(buffer, expected);
    assert.ok(requests[0].endsWith('.ogg'));
    assert.ok(requests[1].endsWith('.mp3'));
    await assert.rejects(loadPianoSample({ decodeAudioData: async () => { throw new Error('decode failed'); } }, 60), /decode failed/);
  } finally { globalThis.fetch = previous; }
});
