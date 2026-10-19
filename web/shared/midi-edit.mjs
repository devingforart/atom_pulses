// Edit note-off times in a Type 0/1 MIDI without discarding tempo, controllers,
// program changes, expression, track names, or any unrelated MIDI event.
import { parseMidi } from './midi.mjs';

const u32 = value => [(value >>> 24) & 255, (value >>> 16) & 255,
  (value >>> 8) & 255, value & 255];
function vlq(value) {
  if (!Number.isInteger(value) || value < 0 || value > 0x0fffffff)
    throw new Error('Invalid MIDI delta');
  const result = [value & 127];
  while ((value >>>= 7)) result.unshift((value & 127) | 128);
  return result;
}

function readVlq(bytes, state, end) {
  let value = 0;
  for (let i = 0; i < 4; i++) {
    if (state.offset >= end) throw new Error('Truncated MIDI delta');
    const byte = bytes[state.offset++];
    value = (value << 7) | (byte & 127);
    if (!(byte & 128)) return value;
  }
  throw new Error('Invalid MIDI delta');
}

function parseEvents(bytes, start, end) {
  const state = { offset: start };
  const active = new Map(), notes = [], events = [];
  let tick = 0, running = 0;
  while (state.offset < end) {
    tick += readVlq(bytes, state, end);
    let status = bytes[state.offset++];
    if (status < 0x80) {
      if (!running) throw new Error('Invalid MIDI running status');
      state.offset--;
      status = running;
    } else if (status < 0xf0) running = status;
    let data;
    if (status === 0xff) {
      const kind = bytes[state.offset++];
      const length = readVlq(bytes, state, end);
      if (state.offset + length > end) throw new Error('Truncated MIDI meta event');
      data = [0xff, kind, ...vlq(length), ...bytes.subarray(state.offset, state.offset + length)];
      state.offset += length;
    } else if (status === 0xf0 || status === 0xf7) {
      const length = readVlq(bytes, state, end);
      if (state.offset + length > end) throw new Error('Truncated MIDI SysEx');
      data = [status, ...vlq(length), ...bytes.subarray(state.offset, state.offset + length)];
      state.offset += length;
    } else {
      const kind = status & 0xf0;
      if (kind < 0x80 || kind > 0xe0) throw new Error('Invalid MIDI channel event');
      const length = kind === 0xc0 || kind === 0xd0 ? 1 : 2;
      if (state.offset + length > end) throw new Error('Truncated MIDI channel event');
      data = [status, ...bytes.subarray(state.offset, state.offset + length)];
      state.offset += length;
    }
    const event = { tick, data, order: events.length };
    events.push(event);
    const kind = status & 0xf0;
    if (status >= 0xf0 || (kind !== 0x80 && kind !== 0x90)) continue;
    const key = `${status & 15}:${data[1]}`;
    if (kind === 0x90 && data[2] > 0) {
      const queue = active.get(key) || [];
      queue.push({ startTick: tick, pitch: data[1], on: event });
      active.set(key, queue);
    } else {
      const queue = active.get(key);
      if (queue?.length) {
        const note = queue.shift();
        notes.push({ ...note, endTick: tick, off: event });
      }
    }
  }
  if (notes.some(note => !note.off) || [...active.values()].some(queue => queue.length))
    throw new Error('Cannot edit MIDI with hanging notes');
  notes.sort((a, b) => a.startTick - b.startTick || a.pitch - b.pitch || a.on.order - b.on.order);
  return { events, notes };
}

export function changeMidiNoteDurations(input, edits) {
  const bytes = input instanceof Uint8Array ? input : new Uint8Array(input);
  const source = parseMidi(bytes);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  if (bytes.length < 14 || view.getUint32(0) !== 0x4d546864)
    throw new Error('Invalid MIDI header');
  const headerEnd = 8 + view.getUint32(4);
  const result = [bytes.slice(0, headerEnd)];
  const byTrack = new Map();
  for (const edit of edits) {
    if (!Number.isInteger(edit.trackIndex) || !Number.isInteger(edit.noteIndex) ||
        !Number.isFinite(edit.durationBeats) || edit.durationBeats <= 0 ||
        !source.tracks[edit.trackIndex]?.notes[edit.noteIndex])
      throw new Error('Invalid MIDI note-duration edit');
    const group = byTrack.get(edit.trackIndex) || new Map();
    if (group.has(edit.noteIndex)) throw new Error('Duplicate MIDI note-duration edit');
    group.set(edit.noteIndex, edit.durationBeats);
    byTrack.set(edit.trackIndex, group);
  }
  let offset = headerEnd;
  for (let trackIndex = 0; trackIndex < source.tracks.length; trackIndex++) {
    if (offset + 8 > bytes.length || view.getUint32(offset) !== 0x4d54726b)
      throw new Error('Invalid MIDI track');
    const end = offset + 8 + view.getUint32(offset + 4);
    if (end > bytes.length) throw new Error('Truncated MIDI track');
    const group = byTrack.get(trackIndex);
    if (!group) { result.push(bytes.slice(offset, end)); offset = end; continue; }
    const { events, notes } = parseEvents(bytes, offset + 8, end);
    if (notes.length !== source.tracks[trackIndex].notes.length)
      throw new Error('MIDI parser note count mismatch');
    for (const [noteIndex, duration] of group) {
      const note = notes[noteIndex];
      const parsed = source.tracks[trackIndex].notes[noteIndex];
      if (note.pitch !== parsed.pitch ||
          Math.abs(note.startTick / source.division - parsed.startBeat) > 1e-6 ||
          Math.abs((note.endTick - note.startTick) / source.division - parsed.durationBeats) > 1e-6)
        throw new Error('MIDI parser note identity mismatch');
      const endTick = Math.max(note.startTick + 1,
        Math.round((parsed.startBeat + duration) * source.division));
      note.off.tick = endTick;
    }
    const endOfTrack = events.find(event => event.data[0] === 0xff && event.data[1] === 0x2f);
    if (!endOfTrack) throw new Error('MIDI track has no end marker');
    endOfTrack.tick = Math.max(endOfTrack.tick,
      ...events.filter(event => event !== endOfTrack).map(event => event.tick));
    events.sort((a, b) => a.tick - b.tick || a.order - b.order);
    const payload = [];
    let previous = 0;
    for (const event of events) {
      payload.push(...vlq(event.tick - previous), ...event.data);
      previous = event.tick;
    }
    result.push(Uint8Array.from([0x4d, 0x54, 0x72, 0x6b,
      ...u32(payload.length), ...payload]));
    offset = end;
  }
  if (offset !== bytes.length) throw new Error('Unexpected data after MIDI tracks');
  const output = new Uint8Array(result.reduce((sum, chunk) => sum + chunk.length, 0));
  let cursor = 0;
  for (const chunk of result) { output.set(chunk, cursor); cursor += chunk.length; }
  return output;
}
