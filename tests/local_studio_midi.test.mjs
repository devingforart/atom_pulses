import test from 'node:test';
import assert from 'node:assert/strict';
import { parseMidi } from '../web/shared/midi.mjs';
import { instrumentFor } from '../web/shared/audio-engine.mjs';
import { ProductionAudio } from '../web/shared/production-audio.mjs';
import { PATCHES, patchForTrack, patchOptions } from '../web/shared/sound-palette.mjs';

const u16 = number => [(number >> 8) & 255, number & 255];
const u32 = number => [(number >>> 24) & 255, (number >>> 16) & 255, (number >>> 8) & 255, number & 255];
const chunk = (name, bytes) => [...Buffer.from(name), ...u32(bytes.length), ...bytes];
const midi = (...tracks) => Uint8Array.from([...chunk('MThd', [...u16(tracks.length > 1 ? 1 : 0), ...u16(tracks.length), ...u16(480)]), ...tracks.flatMap(track => chunk('MTrk', track))]);

test('lee notas reales, running status, tempo y compás', () => {
  const bytes = midi(
    [0, 0xff, 0x51, 3, 0x07, 0xa1, 0x20, 0, 0xff, 0x58, 4, 3, 2, 24, 8, 0, 0xff, 0x2f, 0],
    [0, 0x90, 60, 100, 0x83, 0x60, 60, 0, 0, 0x90, 64, 90, 0x83, 0x60, 0x80, 64, 0, 0, 0xff, 0x2f, 0],
  );
  const score = parseMidi(bytes);
  assert.equal(score.tracks.length, 2);
  assert.equal(score.beatsPerBar, 3);
  assert.equal(score.tracks[1].notes.length, 2);
  assert.deepEqual(score.tracks[1].notes.map(note => [note.pitch, note.startBeat, note.durationBeats]), [[60, 0, 1], [64, 1, 1]]);
  assert.equal(score.beatToSeconds(2), 1);
  assert.equal(score.secondsToBeat(1), 2);
});

test('distingue sonidos por rol y no por una lista fija de pistas', () => {
  assert.equal(instrumentFor({ catalog_id: 'analog_pad', department: 'harmony' }, 'Central chord bed'), 'pad');
  assert.equal(instrumentFor({ catalog_id: 'sub_synth', department: 'harmony' }, 'Low end'), 'bass');
  assert.equal(instrumentFor({ department: 'rhythm' }, 'Arbitrary name'), 'drums');
});

test('rechaza un MIDI truncado en lugar de mostrar notas inventadas', () => {
  const bytes = midi([0, 0x90, 60, 100, 0x81, 0x80, 0x80, 60, 0]);
  assert.throws(() => parseMidi(bytes.subarray(0, bytes.length - 1)), /incomplet/);
});

test('elige un sonido por función musical y limita los cambios a su familia', () => {
  assert.equal(patchForTrack({ name: 'Acordes', meta: { catalog_id: 'analog_pad', source_voice: 'harmonic_foundation' } }), 'pad_velvet');
  assert.equal(patchForTrack({ name: 'Pedal', meta: { catalog_id: 'sub_synth', source_voice: 'sub_bass' } }), 'sub_round');
  assert.equal(patchForTrack({ name: 'Hi hats', meta: { catalog_id: 'hi_hats', department: 'rhythm' } }), 'drums_electronic');
  assert.equal(patchForTrack({ name: 'Secuencia FM', meta: { catalog_id: 'fm_sequence' } }), 'arp_glass');
  assert.ok(patchOptions('pad').length >= 3);
  assert.ok(Object.values(PATCHES).every(patch => patch.label && patch.family && Number.isFinite(patch.level)));
  const player = new ProductionAudio();
  const score = { beatToSeconds: value => value, lengthBeats: 4 };
  const track = { name: 'Pad', filename: 'pad.mid', instrument: 'pad', meta: { catalog_id: 'analog_pad' }, notes: [] };
  player.setSong(score, [track]);
  assert.equal(track.patchId, 'pad_velvet');
  assert.equal(player.setPatch(0, 'pad_glass'), true);
  assert.equal(player.setPatch(0, 'bass_reese'), false);
  assert.equal(track.patchId, 'pad_glass');
  player.setMode('neutral');
  assert.equal(player.mode, 'neutral');
  player.setMute(0, true);
  player.setSong(score, [track]);
  assert.equal(player.muted.size, 0);
});
