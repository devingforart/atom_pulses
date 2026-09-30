// Neutral sine-wave audition. Sound categories only identify percussion; every
// pitched part uses the same oscillator/envelope, without timbral presets.
const clamp = (value, min, max) => Math.min(max, Math.max(min, value));
const frequency = pitch => 440 * 2 ** ((pitch - 69) / 12);

export function instrumentFor(part = {}, name = '') {
  const text = `${part.catalog_id || ''} ${part.source_voice || ''} ${part.role || ''} ${name}`.toLowerCase();
  if (part.department === 'rhythm' || /kick|snare|clap|hat|cymbal|drum|percussion|tom|shaker/.test(text)) return 'drums';
  if (/sub|bass|low.end|pedal/.test(text)) return 'bass';
  if (/piano|keys|rhodes|electric.piano/.test(text)) return 'keys';
  if (/pluck|arp|harp|marimba|bell/.test(text)) return 'pluck';
  if (/pad|atmos|texture|bed|chord|wash|string/.test(text)) return 'pad';
  if (/lead|protagonist|melody|melodic|answer|dialogue|flute/.test(text)) return 'lead';
  return 'synth';
}

export class SuiteAudio {
  constructor(onPosition, onStop) {
    this.onPosition = onPosition;
    this.onStop = onStop;
    this.tracks = [];
    this.muted = new Set();
    this.soloed = new Set();
    this.position = 0;
    this.playing = false;
    this.voices = new Set();
    this.masterVolume = 0.68;
  }
  setSong(midi, tracks) { this.stop(); this.midi = midi; this.tracks = tracks; this.position = 0; this.onPosition?.(0); }
  async ensureAudio() {
    if (!this.context) {
      this.context = new AudioContext({ latencyHint: 'interactive' });
      this.master = this.context.createGain();
      this.master.gain.value = this.masterVolume;
      this.limiter = this.context.createDynamicsCompressor();
      this.limiter.threshold.value = -6;
      this.limiter.knee.value = 3;
      this.limiter.ratio.value = 8;
      this.limiter.attack.value = 0.003;
      this.limiter.release.value = 0.18;
      this.master.connect(this.limiter).connect(this.context.destination);
    }
    if (this.context.state !== 'running') await this.context.resume();
  }
  setVolume(value) { this.masterVolume = clamp(Number(value) || 0, 0, 1); if (this.master) this.master.gain.setTargetAtTime(this.masterVolume, this.context.currentTime, 0.015); }
  audible(index) { return !this.muted.has(index) && (!this.soloed.size || this.soloed.has(index)); }
  setMute(index, value) { value ? this.muted.add(index) : this.muted.delete(index); this.reschedule(); }
  setSolo(index, value) { value ? this.soloed.add(index) : this.soloed.delete(index); this.reschedule(); }
  reschedule() { if (this.playing) { const position = this.currentPosition(); this.stopVoices(); this.position = position; this.origin = this.context.currentTime - position; this.cursors = this.tracks.map(track => this.firstAfter(track.notes, position)); this.scheduleHeldNotes(); this.schedule(); } }
  firstAfter(notes, seconds) {
    let low = 0, high = notes.length;
    while (low < high) { const mid = (low + high) >> 1; if (this.midi.beatToSeconds(notes[mid].startBeat) < seconds - 0.001) low = mid + 1; else high = mid; }
    return low;
  }
  currentPosition() { return this.playing ? clamp(this.context.currentTime - this.origin, 0, this.duration) : this.position; }
  get duration() { return this.midi ? this.midi.beatToSeconds(this.midi.lengthBeats) : 0; }
  async play() {
    if (!this.midi || !this.tracks.length) return;
    await this.ensureAudio();
    if (this.playing) return;
    if (this.position >= this.duration - 0.01) this.position = 0;
    this.origin = this.context.currentTime - this.position;
    this.cursors = this.tracks.map(track => this.firstAfter(track.notes, this.position));
    this.playing = true;
    this.scheduleHeldNotes();
    this.schedule();
    this.timer = setInterval(() => this.schedule(), 25);
    this.frame = requestAnimationFrame(() => this.animate());
  }
  pause() {
    if (!this.playing) return;
    this.position = this.currentPosition();
    this.playing = false;
    clearInterval(this.timer);
    cancelAnimationFrame(this.frame);
    this.stopVoices();
    this.onPosition?.(this.position);
  }
  stop() { this.pause(); this.position = 0; this.stopVoices(); this.onPosition?.(0); this.onStop?.(); }
  seek(seconds) {
    this.position = clamp(seconds, 0, this.duration);
    if (this.playing) {
      this.stopVoices();
      this.origin = this.context.currentTime - this.position;
      this.cursors = this.tracks.map(track => this.firstAfter(track.notes, this.position));
      this.scheduleHeldNotes();
      this.schedule();
    }
    this.onPosition?.(this.position);
  }
  animate() {
    if (!this.playing) return;
    const position = this.currentPosition();
    this.onPosition?.(position);
    if (position >= this.duration - 0.005) { this.stop(); return; }
    this.frame = requestAnimationFrame(() => this.animate());
  }
  schedule() {
    if (!this.playing) return;
    const now = this.context.currentTime, end = now + 0.14;
    for (let index = 0; index < this.tracks.length; index++) {
      const track = this.tracks[index], notes = track.notes;
      let cursor = this.cursors[index] || 0;
      while (cursor < notes.length) {
        const note = notes[cursor], when = this.origin + this.midi.beatToSeconds(note.startBeat);
        if (when > end) break;
        if (this.audible(index) && when >= now - 0.02) {
          const endAt = this.origin + this.midi.beatToSeconds(note.startBeat + note.durationBeats);
          this.note(track.instrument, note, Math.max(now, when), Math.max(0.035, endAt - when));
        }
        cursor++;
      }
      this.cursors[index] = cursor;
    }
  }
  scheduleHeldNotes() {
    if (!this.playing || this.position <= 0) return;
    const now = this.context.currentTime;
    this.tracks.forEach((track, index) => {
      if (!this.audible(index) || track.instrument === 'drums') return;
      for (const note of track.notes) {
        const start = this.midi.beatToSeconds(note.startBeat);
        if (start >= this.position) break;
        const end = this.midi.beatToSeconds(note.startBeat + note.durationBeats);
        if (end > this.position + 0.03) this.note(track.instrument, note, now, end - this.position);
      }
    });
  }
  stopVoices() { for (const voice of [...this.voices]) voice.stop(); this.voices.clear(); }
  note(instrument, note, at, seconds) {
    if (this.voices.size >= 96) return;
    if (instrument === 'drums' || note.channel === 9) return this.drum(note, at);
    const context = this.context;
    const velocity = clamp(note.velocity / 127, 0.04, 1);
    const attack = 0.008, release = 0.045, level = 0.085 * velocity;
    const releaseAt = at + Math.max(attack + 0.02, seconds);
    const gain = context.createGain(), oscillator = context.createOscillator();
    oscillator.type = 'sine';
    oscillator.frequency.setValueAtTime(frequency(note.pitch), at);
    gain.gain.setValueAtTime(0, at);
    gain.gain.linearRampToValueAtTime(level, at + attack);
    gain.gain.setValueAtTime(level, releaseAt);
    gain.gain.linearRampToValueAtTime(0, releaseAt + release);
    oscillator.connect(gain).connect(this.master);
    oscillator.start(at);
    oscillator.stop(releaseAt + release + 0.01);
    const voice = { stop: () => { try { gain.gain.cancelScheduledValues(context.currentTime); gain.gain.setTargetAtTime(0, context.currentTime, 0.004); oscillator.stop(context.currentTime + 0.03); } catch {} } };
    this.voices.add(voice);
    oscillator.onended = () => { this.voices.delete(voice); oscillator.disconnect(); gain.disconnect(); };
  }
  drum(note, at) {
    const context = this.context, pitch = note.pitch;
    const kick = [35, 36].includes(pitch), snare = [38, 40].includes(pitch), hat = [42, 44, 46].includes(pitch);
    const length = kick ? 0.13 : snare ? 0.075 : hat ? 0.035 : 0.055;
    const pitchHz = kick ? 92 : snare ? 220 : hat ? 950 : 390;
    const gain = context.createGain();
    gain.gain.setValueAtTime(0, at);
    gain.gain.linearRampToValueAtTime(0.11 * note.velocity / 127, at + 0.002);
    gain.gain.linearRampToValueAtTime(0, at + length);
    gain.connect(this.master);
    const source = context.createOscillator();
    source.type = 'sine';
    source.frequency.setValueAtTime(pitchHz, at);
    source.connect(gain);
    source.start(at); source.stop(at + length + 0.01);
    const voice = { stop: () => { try { source.stop(); } catch {} } };
    this.voices.add(voice);
    source.onended = () => { this.voices.delete(voice); source.disconnect(); gain.disconnect(); };
  }
}
