// Read-only inspection of a local-studio rejected MIDI draft. This is an
// interval inventory, not a substitute for the C++ tonal/narrative auditor.
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../web/shared/midi.mjs';

const [directory, voiceQuery] = process.argv.slice(2);
if (!directory || !voiceQuery) {
  throw new Error('Usage: node tools/audit-rejected-voice.mjs <job-directory> <voice-name>');
}
const diagnostic = JSON.parse(readFileSync(path.join(directory, 'diagnostic.json'), 'utf8'));
const midi = parseMidi(readFileSync(path.join(directory, diagnostic.file)));
const target = midi.tracks.find(track => track.name.toLowerCase().includes(voiceQuery.toLowerCase()));
if (!target) throw new Error(`Voice ${voiceQuery} not found in ${diagnostic.file}`);
const notes = [...target.notes].sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch);
const pitchCounts = new Map();
const intervalPairs = new Map();
const byOtherTrack = new Map();
let repeatedTwoBarNotes = 0;
for (let index = 0; index < notes.length; index += 1) {
  const note = notes[index];
  pitchCounts.set(note.pitch, (pitchCounts.get(note.pitch) || 0) + 1);
  if (index && note.pitch === notes[index - 1].pitch &&
      Math.abs(note.startBeat - notes[index - 1].startBeat - 8) < .01) repeatedTwoBarNotes += 1;
  for (const otherTrack of midi.tracks) {
    if (otherTrack === target) continue;
    for (const other of otherTrack.notes) {
      const overlap = Math.min(note.startBeat + note.durationBeats,
        other.startBeat + other.durationBeats) - Math.max(note.startBeat, other.startBeat);
      if (overlap < .5) continue;
      const interval = Math.abs(note.pitch - other.pitch) % 12;
      if (![1, 6, 11].includes(interval)) continue;
      const key = `${note.pitch}/${other.pitch}`;
      const entry = intervalPairs.get(key) || { pitch: note.pitch, otherPitch: other.pitch,
        events: 0, beats: 0, examples: [] };
      entry.events += 1;
      entry.beats += overlap;
      if (entry.examples.length < 4) entry.examples.push(Number(note.startBeat.toFixed(2)));
      intervalPairs.set(key, entry);
      byOtherTrack.set(otherTrack.name, (byOtherTrack.get(otherTrack.name) || 0) + 1);
    }
  }
}
const top = values => [...values].sort((a, b) => b[1] - a[1]);
console.log(JSON.stringify({ voice: target.name, notes: notes.length,
  repeatedTwoBarNotes, repeatedTwoBarRatio: Number((repeatedTwoBarNotes / Math.max(1, notes.length)).toFixed(3)),
  pitchCounts: top(pitchCounts),
  conflictingTracks: top(byOtherTrack),
  conflictingPairs: [...intervalPairs.values()].sort((a, b) => b.events - a.events)
    .slice(0, 20).map(entry => ({ ...entry, beats: Number(entry.beats.toFixed(2)) })),
}, null, 2));
