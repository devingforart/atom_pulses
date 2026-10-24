import { readFileSync } from 'node:fs';
import { parseMidi } from '../web/shared/midi.mjs';

const [sourcePath, revisedPath, ownerName, startBeatText, barsText] = process.argv.slice(2);
if (!sourcePath || !revisedPath || !ownerName || !startBeatText || !barsText) {
  throw new Error('usage: node tests/verify_harmonic_phrase_revision.mjs SOURCE REVISED OWNER START_BEAT BARS');
}
const source = parseMidi(readFileSync(sourcePath));
const revised = parseMidi(readFileSync(revisedPath));
const start = Number(startBeatText);
const end = start + Number(barsText) * source.beatsPerBar;
if (source.tracks.length !== revised.tracks.length) throw new Error('Track count changed');
const changed = [];
for (let index = 0; index < source.tracks.length; index++) {
  const before = source.tracks[index];
  const after = revised.tracks[index];
  if (before.name !== after.name) throw new Error(`Track ${index} name changed`);
  if (JSON.stringify(before.notes) === JSON.stringify(after.notes)) continue;
  changed.push({ index, name: after.name, before: before.notes.length, after: after.notes.length });
  if (!after.name.includes(ownerName)) throw new Error(`Unrelated track ${after.name} changed`);
  const outside = notes => notes.filter(note => note.startBeat < start || note.startBeat >= end);
  if (JSON.stringify(outside(before.notes)) !== JSON.stringify(outside(after.notes)))
    throw new Error(`Owner track changed outside the requested window`);
  for (let bar = 0; bar < Number(barsText); bar++) {
    const barStart = start + bar * source.beatsPerBar;
    if (!after.notes.some(note => note.startBeat < barStart + source.beatsPerBar &&
        note.startBeat + note.durationBeats >= barStart + source.beatsPerBar / 2))
      throw new Error(`Revised owner has no sustained body in bar ${bar + 1}`);
  }
}
if (changed.length !== 1) throw new Error(`Expected one changed track, found ${changed.length}`);
console.log(JSON.stringify({ changed, bars: Number(barsText), preservedOtherTracks: source.tracks.length - 1 }));
