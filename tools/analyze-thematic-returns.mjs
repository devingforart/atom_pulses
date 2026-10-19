// Read-only evidence for listening comparisons; never edits MIDI.
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../web/shared/midi.mjs';

const directory = process.argv[2];
const stem = process.argv[3] || 'full-song';
if (!directory || !/^(?:full-song|partial-\d{2})$/.test(stem))
  throw new Error('Usage: node tools/analyze-thematic-returns.mjs <job-directory> [partial-XX]');
const planFile = stem === 'full-song' ? 'composition-plan.json' : `${stem}-plan.json`;
const plan = JSON.parse(readFileSync(path.join(directory, planFile), 'utf8'));
const midi = parseMidi(readFileSync(path.join(directory, `${stem}.mid`)));
const owner = plan.cast.find(part => part.id === plan.protagonist_id);
if (!owner) throw new Error('The composition has no declared protagonist');
const track = midi.tracks.find(item => item.name === owner.name ||
  item.name === `PULSO ${owner.name}`);
if (!track?.notes.length) throw new Error('The protagonist has no audible MIDI');
const notes = [...track.notes].sort((left, right) => left.startBeat - right.startBeat ||
  left.pitch - right.pitch);
const motif = notes.slice(0, 3);
const near = (a, b) => Math.abs(a - b) < 0.01;
function matchesAt(index) {
  if (index + motif.length > notes.length) return false;
  return motif.every((reference, offset) => {
    const current = notes[index + offset];
    return current.pitch === reference.pitch &&
      near(current.durationBeats, reference.durationBeats) &&
      near(current.startBeat - notes[index].startBeat,
        reference.startBeat - motif[0].startBeat);
  });
}
const sections = plan.sections.map(section => {
  const start = section.start_bar * plan.beats_per_bar;
  const end = (section.start_bar + section.bars) * plan.beats_per_bar;
  const inSection = notes.filter(note => note.startBeat >= start && note.startBeat < end);
  const literalReturns = notes.reduce((count, note, index) =>
    count + Number(note.startBeat >= start && note.startBeat < end && matchesAt(index)), 0);
  const pitchTriples = new Set(inSection.slice(0, -2).map((note, index) =>
    [note.pitch, inSection[index + 1].pitch, inSection[index + 2].pitch].join(',')));
  return { name: section.name, bars: section.bars, protagonistAttacks: inSection.length,
    exactOpeningReturns: literalReturns, distinctPitchTriples: pitchTriples.size,
    firstAttackBeat: inSection[0]?.startBeat ?? null,
    lastAttackBeat: inSection.at(-1)?.startBeat ?? null };
});
console.log(JSON.stringify({ title: plan.title, protagonist: track.name,
  openingThreeNotes: motif.map(note => ({ beat: note.startBeat,
    pitch: note.pitch, duration: note.durationBeats })),
  exactOpeningReturns: sections.reduce((sum, section) =>
    sum + section.exactOpeningReturns, 0), sections }, null, 2));
