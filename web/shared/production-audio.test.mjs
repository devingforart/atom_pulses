import assert from 'node:assert/strict';
import { test } from 'node:test';
import { ProductionAudio } from './production-audio.mjs';

test('full WAV spans the whole score in bounded slices and preserves its header', async () => {
  const previous = globalThis.OfflineAudioContext;
  const rendered = [];
  class FakeOfflineAudioContext {
    constructor(channels, length, sampleRate) {
      assert.equal(channels, 2);
      this.length = length;
      this.sampleRate = sampleRate;
      this.destination = {};
      this.state = 'running';
      rendered.push(length);
    }
    createGain() { return { gain: { value: 0 }, connect() { return this; } }; }
    createDynamicsCompressor() { return { threshold: {}, ratio: {}, attack: {}, release: {}, connect() { return this; } }; }
    createConvolver() { return { buffer: null, connect() { return this; } }; }
    createBuffer(channels, length, sampleRate) {
      const data = Array.from({ length: channels }, () => new Float32Array(length));
      return { length, sampleRate, getChannelData: channel => data[channel] };
    }
    async startRendering() { return this.createBuffer(2, this.length, this.sampleRate); }
    async close() { this.state = 'closed'; }
  }
  globalThis.OfflineAudioContext = FakeOfflineAudioContext;
  try {
    const audio = new ProductionAudio();
    audio.mode = 'neutral';
    audio.midi = { lengthBeats: 20.25, beatToSeconds: beat => beat };
    audio.tracks = [{ instrument: 'lead', notes: [] }];
    const progress = [];
    const blob = await audio.renderFullWav(value => progress.push(value));
    const expectedFrames = Math.ceil((20.25 + 2.5) * 44100);
    assert.equal(rendered.length, 2);
    assert.ok(rendered.every(length => length <= 24 * 44100));
    assert.equal(progress.length, 2);
    assert.ok(Math.abs(progress[0] - 20 / 22.75) < 1e-10);
    assert.equal(progress[1], 1);
    assert.equal(blob.size, 44 + expectedFrames * 4);
    const bytes = new DataView(await blob.slice(0, 44).arrayBuffer());
    const ascii = (offset, length) => String.fromCharCode(...Array.from({ length }, (_, index) => bytes.getUint8(offset + index)));
    assert.equal(ascii(0, 4), 'RIFF');
    assert.equal(ascii(8, 4), 'WAVE');
    assert.equal(bytes.getUint32(24, true), 44100);
    assert.equal(bytes.getUint16(22, true), 2);
    assert.equal(bytes.getUint16(34, true), 16);
    assert.equal(bytes.getUint32(40, true), expectedFrames * 4);
  } finally { globalThis.OfflineAudioContext = previous; }
});
