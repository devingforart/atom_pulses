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
