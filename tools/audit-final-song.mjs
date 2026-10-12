// Read-only structural audit of an exported local-studio MIDI song.
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../web/shared/midi.mjs';

const directory = process.argv[2];
if (!directory) throw new Error('Usage: node tools/audit-final-song.mjs <job-directory>');
const midi = parseMidi(readFileSync(path.join(directory, 'full-song.mid')));
const plan = JSON.parse(readFileSync(path.join(directory, 'composition-plan.json'), 'utf8'));
const audit = JSON.parse(readFileSync(path.join(directory, 'musical-audit.json'), 'utf8'));
const sections = plan.sections.map(section => ({
  name: section.name,
  startBeat: section.start_bar * plan.beats_per_bar,
  endBeat: (section.start_bar + section.bars) * plan.beats_per_bar,
}));
const tracks = midi.tracks.filter(track => track.notes.length).map(track => {
  const counts = sections.map(section => track.notes.filter(note =>
    note.startBeat >= section.startBeat && note.startBeat < section.endBeat).length);
  const last = track.notes.at(-1);
  const intervals = track.notes.slice(1).map((note, index) => ({
    distance: Math.abs(note.pitch - track.notes[index].pitch),
    gap: note.startBeat - track.notes[index].startBeat,
  })).filter(pair => pair.gap > 0 && pair.gap <= plan.beats_per_bar * .75);
  const steps = intervals.filter(pair => pair.distance <= 2).length;
  return { name: track.name, notes: track.notes.length, counts,
    sectionCount: counts.filter(Boolean).length,
    firstBeat: track.notes[0].startBeat,
    lastBeat: last.startBeat,
    stepRatio: intervals.length ? Number((steps / intervals.length).toFixed(3)) : null,
    unterminated: track.notes.filter(note => note.unterminated).length };
});
const protagonistName = plan.cast.find(part => part.id === plan.protagonist_id)?.name;
const protagonist = midi.tracks.find(track => track.name === protagonistName ||
  track.name === `PULSO ${protagonistName}`);
const firstThematicSection = sections.find(section => protagonist?.notes.some(note =>
  note.startBeat >= section.startBeat && note.startBeat < section.endBeat));
const premise = protagonist?.notes.filter(note => note.startBeat >= firstThematicSection?.startBeat &&
  note.startBeat < firstThematicSection?.endBeat) || [];
const coda = protagonist?.notes.filter(note => note.startBeat >= midi.lengthBeats - 32) || [];
const compact = notes => notes.slice(0, 24).map(note => [
  Number(note.startBeat.toFixed(2)), note.pitch, Number(note.durationBeats.toFixed(2))]);
console.log(JSON.stringify({ title: plan.title, bars: plan.total_bars,
  seconds: midi.beatToSeconds(midi.lengthBeats), trackCount: tracks.length,
  notes: tracks.reduce((sum, track) => sum + track.notes, 0),
  sections: sections.map((section, index) => ({ name: section.name,
    activeTracks: tracks.filter(track => track.counts[index]).length,
    notes: tracks.reduce((sum, track) => sum + track.counts[index], 0) })),
  narrativeReady: audit.narrative_ready, narrativeIssues: audit.narrative_issues,
  soundscapeReady: audit.soundscape_ready, soundscapeIssues: audit.soundscape_issues,
  protagonist: protagonist?.name, firstThematicSection: firstThematicSection?.name,
  protagonistPremise: compact(premise),
  protagonistCoda: compact(coda), tracks: tracks }, null, 2));
