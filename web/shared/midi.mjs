// Standard MIDI File reader shared by local and Cloud suites and their tests.
// Times are kept in quarter-note beats; tempo events supply exact seconds.
export function parseMidi(input) {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  let cursor = 0;
  const requireBytes = count => { if (cursor + count > bytes.length) throw new Error('MIDI incompleto'); };
  const u8 = () => { requireBytes(1); return bytes[cursor++]; };
  const u16 = () => { requireBytes(2); const value = view.getUint16(cursor); cursor += 2; return value; };
  const u32 = () => { requireBytes(4); const value = view.getUint32(cursor); cursor += 4; return value; };
  const text = count => { requireBytes(count); const value = String.fromCharCode(...bytes.subarray(cursor, cursor + Math.min(count, 4096))); cursor += count; return value; };
  const variable = end => { let value = 0; for (let i = 0; i < 4; i++) { if (cursor >= end) throw new Error('MIDI VLQ incompleto'); const byte = u8(); value = (value << 7) | (byte & 127); if (!(byte & 128)) return value; } throw new Error('MIDI VLQ inválido'); };
  if (text(4) !== 'MThd') throw new Error('No es un archivo MIDI');
  const headerLength = u32();
  if (headerLength < 6) throw new Error('Cabecera MIDI inválida');
  const format = u16(), trackCount = u16(), division = u16();
  if (format > 1 || (division & 0x8000) || !division || trackCount > 256) throw new Error('Formato MIDI no compatible');
  cursor += headerLength - 6;
  requireBytes(0);
  const tracks = [], tempos = [{ tick: 0, microseconds: 500000 }];
  let beatsPerBar = 4, lastTick = 0;
  for (let trackIndex = 0; trackIndex < trackCount; trackIndex++) {
    if (text(4) !== 'MTrk') throw new Error('Pista MIDI inválida');
    const length = u32(), end = cursor + length;
    if (end > bytes.length) throw new Error('Pista MIDI incompleta');
    let tick = 0, running = 0, name = `Pista ${trackIndex + 1}`;
    const active = new Map(), notes = [];
    while (cursor < end) {
      tick += variable(end);
      if (tick > 1e9) throw new Error('Duración MIDI excesiva');
      lastTick = Math.max(lastTick, tick);
      let status = u8();
      if (status < 0x80) { if (!running) throw new Error('Running status MIDI inválido'); cursor--; status = running; }
      else if (status < 0xf0) running = status;
      if (status === 0xff) {
        const kind = u8(), length = variable(end);
        if (cursor + length > end) throw new Error('Metaevento MIDI incompleto');
        if (kind === 0x51 && length === 3) tempos.push({ tick, microseconds: (bytes[cursor] << 16) | (bytes[cursor + 1] << 8) | bytes[cursor + 2] });
        if (kind === 0x58 && length >= 2 && trackIndex === 0) beatsPerBar = bytes[cursor] * 4 / (2 ** bytes[cursor + 1]);
        if (kind === 0x03 && length) name = new TextDecoder().decode(bytes.subarray(cursor, cursor + length));
        cursor += length;
        if (kind === 0x2f) { cursor = end; break; }
        continue;
      }
      if (status === 0xf0 || status === 0xf7) { const length = variable(end); cursor += length; if (cursor > end) throw new Error('SysEx MIDI incompleto'); continue; }
      const type = status & 0xf0, channel = status & 15;
      if (type < 0x80 || type > 0xe0) throw new Error('Evento MIDI no compatible');
      const first = u8();
      const second = type === 0xc0 || type === 0xd0 ? 0 : u8();
      if (cursor > end) throw new Error('Evento MIDI incompleto');
      if (type !== 0x80 && type !== 0x90) continue;
      const key = `${channel}:${first}`;
      if (type === 0x90 && second > 0) {
        const queue = active.get(key) || [];
        queue.push({ startTick: tick, pitch: first, velocity: second, channel });
        active.set(key, queue);
      } else {
        const queue = active.get(key);
        if (queue?.length) {
          const note = queue.shift();
          notes.push({ startBeat: note.startTick / division, durationBeats: Math.max(1 / division, (tick - note.startTick) / division), pitch: note.pitch, velocity: note.velocity, channel });
        }
      }
    }
    // A malformed hanging note is bounded by the file's end instead of sounding forever.
    for (const queue of active.values()) for (const note of queue)
      notes.push({ startBeat: note.startTick / division, durationBeats: Math.max(1 / division, (tick - note.startTick) / division), pitch: note.pitch, velocity: note.velocity, channel, unterminated: true });
    notes.sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch);
    tracks.push({ name, notes });
    cursor = end;
  }
  tempos.sort((a, b) => a.tick - b.tick);
  let seconds = 0;
  for (let i = 0; i < tempos.length; i++) {
    if (i) seconds += (tempos[i].tick - tempos[i - 1].tick) / division * tempos[i - 1].microseconds / 1e6;
    tempos[i].seconds = seconds;
  }
  const beatToSeconds = beat => {
    const tick = Math.max(0, beat * division);
    let low = 0, high = tempos.length - 1;
    while (low < high) { const mid = Math.ceil((low + high) / 2); if (tempos[mid].tick <= tick) low = mid; else high = mid - 1; }
    return tempos[low].seconds + (tick - tempos[low].tick) / division * tempos[low].microseconds / 1e6;
  };
  const secondsToBeat = value => {
    let index = tempos.length - 1;
    while (index > 0 && tempos[index].seconds > value) index--;
    return (tempos[index].tick + Math.max(0, value - tempos[index].seconds) * 1e6 / tempos[index].microseconds * division) / division;
  };
  return { format, division, tracks, beatsPerBar, lengthBeats: lastTick / division, beatToSeconds, secondsToBeat, tempos };
}

// Audition-only Type 0 MIDI: the entire score as one pitched-instrument clip.
// Keep full-song.mid untouched; adding this lane to it would double playback.
const midiClamp = (value, min, max) => Math.min(max, Math.max(min, value));
const midiU16 = value => [(value >>> 8) & 255, value & 255];
const midiU32 = value => [(value >>> 24) & 255, (value >>> 16) & 255, (value >>> 8) & 255, value & 255];
const midiVlq = value => {
  if (!Number.isInteger(value) || value < 0 || value > 0x0fffffff) throw new Error('Tiempo MIDI fuera de rango.');
  const bytes = [value & 127];
  while ((value >>>= 7)) bytes.unshift((value & 127) | 128);
  return bytes;
};

export function singleTrackMidi(score, tracks) {
  const division = score?.division;
  if (!Number.isInteger(division) || division < 1 || division > 0x7fff || !Array.isArray(tracks))
    throw new Error('Partitura MIDI no compatible.');
  const events = [];
  const name = new TextEncoder().encode('PULSO - Todas las notas');
  events.push({ tick: 0, order: 0, bytes: [0xff, 0x03, ...midiVlq(name.length), ...name] });
  const numerator = midiClamp(Math.round(score.beatsPerBar || 4), 1, 255);
  events.push({ tick: 0, order: 1, bytes: [0xff, 0x58, 4, numerator, 2, 24, 8] });
  const tempos = new Map();
  for (const tempo of score.tempos || []) {
    if (Number.isInteger(tempo.tick) && tempo.tick >= 0 && Number.isInteger(tempo.microseconds))
      tempos.set(tempo.tick, midiClamp(tempo.microseconds, 1, 0xffffff));
  }
  if (!tempos.has(0)) tempos.set(0, 500000);
  for (const [tick, microseconds] of tempos)
    events.push({ tick, order: 2, bytes: [0xff, 0x51, 3, (microseconds >>> 16) & 255, (microseconds >>> 8) & 255, microseconds & 255] });

  let noteCount = 0;
  for (const track of tracks) for (const note of track.notes || []) {
    if (!Number.isFinite(note.startBeat) || !Number.isFinite(note.durationBeats) ||
        note.startBeat < 0 || note.durationBeats <= 0 || !Number.isInteger(note.pitch) || note.pitch < 0 || note.pitch > 127) continue;
    const start = Math.round(note.startBeat * division);
    const end = Math.max(start + 1, Math.round((note.startBeat + note.durationBeats) * division));
    const pitch = note.pitch;
    const velocity = midiClamp(Math.round(Number(note.velocity) || 80), 1, 127);
    // Channel 1 makes former drum notes pitched notes on the chosen instrument.
    events.push({ tick: start, order: 4, bytes: [0x90, pitch, velocity] });
    events.push({ tick: end, order: 3, bytes: [0x80, pitch, 0] });
    noteCount++;
  }
  if (!noteCount) throw new Error('Esta obra no tiene notas para combinar.');
  events.sort((a, b) => a.tick - b.tick || a.order - b.order);
  const trackBytes = [];
  let previousTick = 0;
  for (const event of events) {
    trackBytes.push(...midiVlq(event.tick - previousTick), ...event.bytes);
    previousTick = event.tick;
  }
  const songEnd = Math.max(previousTick, Math.round((score.lengthBeats || 0) * division));
  trackBytes.push(...midiVlq(songEnd - previousTick), 0xff, 0x2f, 0);
  return Uint8Array.from([
    0x4d, 0x54, 0x68, 0x64, ...midiU32(6), ...midiU16(0), ...midiU16(1), ...midiU16(division),
    0x4d, 0x54, 0x72, 0x6b, ...midiU32(trackBytes.length), ...trackBytes,
  ]);
}

export async function downloadSingleTrackMidi(sourceUrl, filename) {
  const response = await fetch(sourceUrl, { credentials: 'include', cache: 'no-store' });
  if (!response.ok) throw new Error(`No se pudo leer la obra MIDI (HTTP ${response.status}).`);
  const score = parseMidi(await response.arrayBuffer());
  const bytes = singleTrackMidi(score, score.tracks);
  const url = URL.createObjectURL(new Blob([bytes], { type: 'audio/midi' }));
  try {
    const link = document.createElement('a');
    link.href = url;
    link.download = filename;
    link.click();
  } finally { setTimeout(() => URL.revokeObjectURL(url), 60000); }
}
