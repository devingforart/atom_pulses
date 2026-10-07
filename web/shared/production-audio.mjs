import { SuiteAudio } from './audio-engine.mjs';
import { PATCHES, patchForTrack } from './sound-palette.mjs';

const clamp = (value, min, max) => Math.min(max, Math.max(min, value));
const hertz = pitch => 440 * 2 ** ((pitch - 69) / 12);

// Local audition engine. Its only input is the published MIDI and track metadata.
// Sound changes never alter the score, exported notes or the composer.
export class ProductionAudio extends SuiteAudio {
  constructor(onPosition, onStop) {
    super(onPosition, onStop);
    this.mode = 'production';
    this.masterVolume = .72;
  }

  setSong(midi, tracks) {
    for (const track of tracks) track.patchId ||= patchForTrack(track);
    super.setSong(midi, tracks);
  }

  setMode(mode) {
    if (mode !== 'production' && mode !== 'neutral') throw new Error('Modo de escucha desconocido');
    if (this.mode === mode) return;
    this.mode = mode;
    this.reschedule();
  }

  setPatch(index, patchId) {
    const track = this.tracks[index];
    if (!track || !PATCHES[patchId]) return false;
    if (PATCHES[patchId].family !== PATCHES[track.patchId]?.family) return false;
    track.patchId = patchId;
    this.reschedule();
    return true;
  }

  async ensureAudio() {
    await super.ensureAudio();
    if (this.productionReady) return;
    this.prepareProduction();
  }

  prepareProduction() {
    const context = this.context;
    this.limiter.threshold.value = -9;
    this.limiter.ratio.value = 5;
    this.limiter.attack.value = .008;
    this.limiter.release.value = .22;
    this.wet = context.createGain();
    this.wet.gain.value = .22;
    this.reverb = context.createConvolver();
    this.reverb.buffer = makeImpulse(context);
    this.reverb.connect(this.wet).connect(this.master);
    this.noise = makeNoise(context);
    this.productionReady = true;
  }

  async renderPreview(startSeconds = 0, maxSeconds = 30) {
    if (!this.midi || !this.tracks.length) throw new Error('Primero abrí una obra MIDI.');
    if (typeof OfflineAudioContext === 'undefined') throw new Error('Este navegador no admite renderizar la vista previa.');
    const start = clamp(Number(startSeconds) || 0, 0, Math.max(0, this.duration - .1));
    const length = Math.min(clamp(Number(maxSeconds) || 30, 1, 60), Math.max(.1, this.duration - start));
    const sampleRate = 44100;
    const context = new OfflineAudioContext(2, Math.ceil((length + .25) * sampleRate), sampleRate);
    const renderer = new ProductionAudio();
    renderer.context = context;
    renderer.midi = this.midi;
    renderer.tracks = this.tracks.map(track => ({ ...track }));
    renderer.origin = -start;
    renderer.offline = true;
    renderer.master = context.createGain();
    renderer.master.gain.value = this.masterVolume;
    renderer.limiter = context.createDynamicsCompressor();
    renderer.master.connect(renderer.limiter).connect(context.destination);
    renderer.prepareProduction();
    let scheduled = 0;
    for (let index = 0; index < renderer.tracks.length; index++) {
      if (!this.audible(index)) continue;
      const track = renderer.tracks[index];
      for (const note of track.notes) {
        const noteStart = renderer.midi.beatToSeconds(note.startBeat);
        if (noteStart >= start + length) break;
        const noteEnd = renderer.midi.beatToSeconds(note.startBeat + note.durationBeats);
        if (noteEnd <= start || (track.instrument === 'drums' && noteStart < start)) continue;
        const at = Math.max(0, noteStart - start);
        const remaining = Math.min(noteEnd - Math.max(noteStart, start), length - at);
        renderer.note(track.instrument, note, at, remaining, track);
        if (++scheduled > 6000) throw new Error('La vista previa contiene demasiados eventos; elegí un pasaje más breve.');
      }
    }
    const buffer = await context.startRendering();
    return new Blob([encodeWav(buffer)], { type: 'audio/wav' });
  }

  note(instrument, note, at, seconds, track = {}) {
    if (this.mode === 'neutral') return super.note(instrument, note, at, seconds);
    if (!this.offline && this.voices.size >= 88) return;
    const patch = PATCHES[track.patchId] || PATCHES[patchForTrack(track)];
    if (patch.family === 'drums' || instrument === 'drums' || note.channel === 9) return this.drum(note, at, patch);
    const context = this.context;
    const velocity = clamp(note.velocity / 127, .08, 1);
    const duration = Math.max(.025, seconds);
    const end = at + duration;
    const release = Math.min(patch.release, Math.max(.06, this.duration - (at - this.origin + duration) + .10));
    const amplitude = patch.level * (.28 + .72 * velocity) * .15;
    const attackEnd = at + Math.min(patch.attack, duration * .48);
    const decayEnd = Math.min(end, attackEnd + patch.decay);
    const envelope = context.createGain();
    envelope.gain.setValueAtTime(0, at);
    envelope.gain.linearRampToValueAtTime(amplitude, attackEnd);
    envelope.gain.linearRampToValueAtTime(amplitude * patch.sustain, Math.max(attackEnd + .001, decayEnd));
    envelope.gain.setValueAtTime(amplitude * patch.sustain, end);
    envelope.gain.linearRampToValueAtTime(0, end + release);

    const filter = context.createBiquadFilter();
    filter.type = 'lowpass';
    filter.Q.value = patch.family === 'bass' ? .8 : .54;
    const filterBase = Math.min(context.sampleRate * .42, patch.cutoff);
    filter.frequency.setValueAtTime(filterBase * (1 + patch.motion * (.4 + velocity)), at);
    if (patch.motion) filter.frequency.exponentialRampToValueAtTime(Math.max(45, filterBase * (1 - patch.motion * .54)), Math.min(end + release, at + Math.max(.08, duration)));

    const pan = context.createStereoPanner();
    pan.pan.value = patch.pan;
    filter.connect(envelope).connect(pan).connect(this.master);
    if (patch.send) { const send = context.createGain(); send.gain.value = patch.send; pan.connect(send).connect(this.reverb); }
    const fundamental = hertz(note.pitch);
    const oscillators = [patch.wave, patch.second].map((wave, index) => {
      const osc = context.createOscillator();
      const level = context.createGain();
      osc.type = wave;
      osc.frequency.setValueAtTime(fundamental, at);
      osc.detune.value = index ? patch.detune : -patch.detune;
      level.gain.value = index ? .27 : .73;
      osc.connect(level).connect(filter);
      osc.start(at);
      osc.stop(end + release + .025);
      return osc;
    });
    const voice = {
      stop: () => {
        try {
          const now = context.currentTime;
          envelope.gain.cancelScheduledValues(now);
          envelope.gain.setTargetAtTime(0, now, .005);
          for (const osc of oscillators) osc.stop(now + .035);
        } catch { /* A voice may already have ended. */ }
      },
    };
    this.voices.add(voice);
    oscillators[0].onended = () => {
      this.voices.delete(voice);
      for (const osc of oscillators) osc.disconnect();
      filter.disconnect(); envelope.disconnect(); pan.disconnect();
    };
  }

  drum(note, at, patch = PATCHES.drums_electronic) {
    if (this.mode === 'neutral') return super.drum(note, at);
    const context = this.context;
    const pitch = note.pitch;
    const vel = clamp(note.velocity / 127, .1, 1);
    const soft = patch === PATCHES.drums_soft ? .72 : 1;
    const is808 = patch === PATCHES.drums_808;
    const is909 = patch === PATCHES.drums_909 || patch === PATCHES.drums_electronic;
    const kick = pitch === 35 || pitch === 36;
    const snare = [37, 38, 39, 40].includes(pitch);
    const hat = [42, 44, 46].includes(pitch);
    const tom = [41, 43, 45, 47, 48, 50].includes(pitch);
    const crash = [49, 51, 52, 55, 57, 59].includes(pitch);
    const duration = kick ? (is808 ? .52 : .30) : snare ? (is808 ? .28 : .20) : hat ? (pitch === 46 ? .36 : is808 ? .12 : .075) : tom ? .33 : crash ? 1.35 : .19;
    const gain = context.createGain();
    const pan = context.createStereoPanner();
    pan.pan.value = hat ? .20 : tom ? -.15 : 0;
    gain.connect(pan).connect(this.master);
    const send = context.createGain(); send.gain.value = kick ? .008 : crash ? .18 : patch.send;
    pan.connect(send).connect(this.reverb);
    const sources = [];
    if (kick || tom) {
      const osc = context.createOscillator();
      osc.type = 'sine';
      osc.frequency.setValueAtTime(kick ? (is808 ? 122 : 178) : 195 - (pitch - 41) * 10, at);
      osc.frequency.exponentialRampToValueAtTime(kick ? (is808 ? 43 : 53) : 80, at + (kick ? (is808 ? .27 : .13) : .20));
      const body = context.createGain();
      body.gain.setValueAtTime(0, at);
      body.gain.linearRampToValueAtTime((kick ? (is808 ? .84 : .64) : .40) * vel * soft, at + .003);
      body.gain.exponentialRampToValueAtTime(.0001, at + duration);
      osc.connect(body).connect(gain);
      osc.start(at); osc.stop(at + duration + .01);
      sources.push(osc);
    }
    if (!kick || snare) {
      const noise = context.createBufferSource();
      noise.buffer = this.noise;
      const filter = context.createBiquadFilter();
      filter.type = hat || crash ? 'highpass' : 'bandpass';
      filter.frequency.value = hat || crash ? (is808 ? 5900 : 7900) : snare ? (is808 ? 1650 : 2400) : 2600;
      filter.Q.value = .72;
      const snap = context.createGain();
      snap.gain.setValueAtTime((hat ? (is808 ? .13 : .19) : crash ? .21 : snare ? (is808 ? .30 : .43) : .18) * vel * soft, at);
      snap.gain.exponentialRampToValueAtTime(.0001, at + duration);
      noise.connect(filter).connect(snap).connect(gain);
      noise.start(at); noise.stop(at + duration + .01);
      sources.push(noise);
    }
    if (kick && is909) {
      const click = context.createBufferSource(); click.buffer = this.noise;
      const high = context.createBiquadFilter(); high.type = 'highpass'; high.frequency.value = 3300;
      const snap = context.createGain();
      snap.gain.setValueAtTime(.08 * vel * soft, at);
      snap.gain.exponentialRampToValueAtTime(.0001, at + .014);
      click.connect(high).connect(snap).connect(gain);
      click.start(at); click.stop(at + .02);
      sources.push(click);
    }
    if (snare) {
      const tone = context.createOscillator();
      tone.type = 'triangle'; tone.frequency.value = pitch === 40 ? 208 : 176;
      const body = context.createGain();
      body.gain.setValueAtTime(.19 * vel * soft, at);
      body.gain.exponentialRampToValueAtTime(.0001, at + .12);
      tone.connect(body).connect(gain); tone.start(at); tone.stop(at + .14);
      sources.push(tone);
    }
    const voice = { stop: () => {
      try { const now = context.currentTime; gain.gain.cancelScheduledValues(now); gain.gain.setTargetAtTime(0, now, .004); for (const source of sources) source.stop(now + .025); } catch { /* Already finished. */ }
    } };
    this.voices.add(voice);
    sources[0].onended = () => { this.voices.delete(voice); for (const source of sources) source.disconnect(); gain.disconnect(); pan.disconnect(); send.disconnect(); };
  }
}

function makeNoise(context) {
  const length = Math.floor(context.sampleRate * 2);
  const buffer = context.createBuffer(1, length, context.sampleRate);
  const data = buffer.getChannelData(0);
  let seed = 0x71ac39;
  for (let i = 0; i < length; i++) { seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5; data[i] = (seed >>> 0) / 2147483648 - 1; }
  return buffer;
}

function makeImpulse(context) {
  const length = Math.floor(context.sampleRate * 1.7);
  const buffer = context.createBuffer(2, length, context.sampleRate);
  let seed = 0x18508f;
  for (let channel = 0; channel < 2; channel++) {
    const data = buffer.getChannelData(channel);
    for (let i = 0; i < length; i++) {
      seed ^= seed << 13; seed ^= seed >>> 17; seed ^= seed << 5;
      const decay = (1 - i / length) ** 2.8;
      data[i] = ((seed >>> 0) / 2147483648 - 1) * decay * .65;
    }
  }
  return buffer;
}

function encodeWav(buffer) {
  const channels = 2;
  const dataSize = buffer.length * channels * 2;
  const bytes = new ArrayBuffer(44 + dataSize);
  const view = new DataView(bytes);
  const word = (offset, value) => view.setUint16(offset, value, true);
  const dword = (offset, value) => view.setUint32(offset, value, true);
  for (const [offset, value] of [[0, 'RIFF'], [8, 'WAVE'], [12, 'fmt '], [36, 'data']])
    for (let index = 0; index < 4; index++) view.setUint8(offset + index, value.charCodeAt(index));
  dword(4, 36 + dataSize); dword(16, 16); word(20, 1); word(22, channels);
  dword(24, buffer.sampleRate); dword(28, buffer.sampleRate * channels * 2);
  word(32, channels * 2); word(34, 16); dword(40, dataSize);
  const left = buffer.getChannelData(0), right = buffer.getChannelData(1);
  let offset = 44;
  for (let index = 0; index < buffer.length; index++) {
    for (const sample of [left[index], right[index]]) {
      const value = clamp(sample, -1, 1);
      view.setInt16(offset, value < 0 ? value * 32768 : value * 32767, true);
      offset += 2;
    }
  }
  return bytes;
}
