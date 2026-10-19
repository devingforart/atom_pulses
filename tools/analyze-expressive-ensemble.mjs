// Read-only evidence for listening comparisons. No composition, quantization,
// scoring threshold or automatic note editing belongs in this report.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseMidi } from '../web/shared/midi.mjs';

const project = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const source = process.argv[2];
if (!source) throw new Error('Usage: node tools/analyze-expressive-ensemble.mjs <job-id-or-directory> [--output file]');
const directory = /^[0-9a-f-]{36}$/i.test(source)
  ? path.join(project, 'build', 'local-studio-data', 'jobs', source) : path.resolve(source);
const read = name => JSON.parse(fs.readFileSync(path.join(directory, name), 'utf8'));
const manifest = read('manifest.json');
const plan = read(manifest.planFile || 'composition-plan.json');
const request = read('job.json');
const midi = parseMidi(fs.readFileSync(path.join(directory, manifest.fullFile || 'full-song.mid')));
const tracks = midi.tracks.filter(track => track.notes.length);
const owners = tracks.map(track => plan.cast.find(part =>
  track.name === part.name || track.name === `PULSO ${part.name}`));
const tracksByOwner = new Map(owners.flatMap((owner, index) => owner ? [[owner.id, tracks[index]]] : []));
const beatRound = value => Math.round(value * 1000000) / 1000000;
const median = values => {
  const sorted = [...values].sort((a, b) => a - b);
  return sorted.length ? (sorted[Math.floor((sorted.length - 1) / 2)] + sorted[Math.floor(sorted.length / 2)]) / 2 : null;
};
const range = notes => notes.length ? notes.reduce((bounds, note) =>
  [Math.min(bounds[0], note.pitch), Math.max(bounds[1], note.pitch)], [127, 0]) : null;
const span = notes => { const bounds = range(notes); return bounds ? bounds[1] - bounds[0] : null; };
const onsetGroups = notes => {
  const groups = [];
  for (const note of notes) {
    const last = groups.at(-1);
    if (last && Math.abs(last.beat - note.startBeat) < .000001) last.notes.push(note);
    else groups.push({ beat: note.startBeat, notes: [note] });
  }
  return groups;
};
const simultaneous = notes => {
  const events = notes.flatMap(note => [[note.startBeat, 1], [note.startBeat + note.durationBeats, -1]])
    .sort((a, b) => a[0] - b[0] || a[1] - b[1]);
  let active = 0, maximum = 0;
  for (const [, delta] of events) { active += delta; maximum = Math.max(maximum, active); }
  return maximum;
};
const pairRepeats = (notes, sameTrack) => {
  const examples = []; let count = 0;
  for (let i = 0; i < notes.length; i++) for (let j = i + 1; j < notes.length; j++) {
    const left = notes[i], right = notes[j];
    const gap = right.startBeat - left.startBeat;
    if (gap > .5 + .000001) break;
    if (left.pitch !== right.pitch || (sameTrack ? left.owner !== right.owner : left.owner === right.owner)) continue;
    if (sameTrack && gap <= .000001) continue;
    count++;
    if (examples.length < 24) examples.push({ beat: right.startBeat,
      bar: Math.floor(right.startBeat / midi.beatsPerBar) + 1, pitch: right.pitch,
      gapBeats: beatRound(gap), owners: [left.owner, right.owner] });
  }
  return { count, examples };
};
const all = tracks.flatMap((track, owner) => track.notes.map(note => ({ ...note, owner: owner + 1 })))
  .sort((a, b) => a.startBeat - b.startBeat || a.owner - b.owner || a.pitch - b.pitch);
const trackEvidence = tracks.map((track, index) => {
  const notes = track.notes, groups = onsetGroups(notes), localSpans = [], shapes = new Map();
  for (let start = 0; start < midi.lengthBeats; start += midi.beatsPerBar * 2) {
    const local = notes.filter(note => note.startBeat >= start && note.startBeat < start + midi.beatsPerBar * 2);
    if (!local.length) continue;
    localSpans.push(span(local));
    const shape = local.map(note => `${beatRound(note.startBeat - start)}/${beatRound(note.durationBeats)}`).join('|');
    const entry = shapes.get(shape) || { shape, count: 0, bars: [] };
    entry.count++; entry.bars.push(Math.floor(start / midi.beatsPerBar) + 1); shapes.set(shape, entry);
  }
  const connectedIntervals = [];
  for (let i = 1; i < groups.length; i++) {
    const left = groups[i - 1], right = groups[i];
    if (left.notes.length !== 1 || right.notes.length !== 1 || right.beat - left.beat > midi.beatsPerBar) continue;
    connectedIntervals.push(Math.abs(right.notes[0].pitch - left.notes[0].pitch));
  }
  return { id: owners[index]?.id || null, name: track.name, notes: notes.length,
    preferredRegister: [owners[index]?.minimum_pitch ?? null, owners[index]?.maximum_pitch ?? null],
    actualRegister: range(notes), totalSemitones: span(notes),
    medianTwoBarSemitones: median(localSpans), maximumTwoBarSemitones: localSpans.reduce((maximum, value) => Math.max(maximum, value), 0),
    simultaneousNotesMaximum: simultaneous(notes), polyphonicAttacks: groups.filter(group => group.notes.length > 1).length,
    connectedIntervals: connectedIntervals.length,
    connectedOctaveOrLargerIntervals: connectedIntervals.filter(interval => interval >= 12).length,
    connectedStepRatio: connectedIntervals.length ? connectedIntervals.filter(interval => interval > 0 && interval <= 2).length / connectedIntervals.length : null,
    commonTwoBarRhythms: [...shapes.values()].sort((a, b) => b.count - a.count).slice(0, 3),
    notesShorterThanHalfBeat: notes.filter(note => note.durationBeats < .5).length,
    lastNoteOffBeat: notes.reduce((latest, note) => Math.max(latest, note.startBeat + note.durationBeats), 0),
  };
});
let phraseArchitecture = null;
if (manifest.phraseArchitectureFile) phraseArchitecture = read(manifest.phraseArchitectureFile);
const phrases = (phraseArchitecture?.phrases || []).map(phrase => {
  const start = (plan.sections[phrase.section_index].start_bar + phrase.local_start_bar) * midi.beatsPerBar;
  const end = start + phrase.bars * midi.beatsPerBar;
  const notes = (tracksByOwner.get(phrase.speaker_id)?.notes || []).filter(note => note.startBeat >= start && note.startBeat < end);
  return { startBar: start / midi.beatsPerBar + 1, bars: phrase.bars, speaker: phrase.speaker_id,
    response: phrase.response_id, intendedRegister: [phrase.register_low, phrase.register_high],
    actualRegister: range(notes), actualSemitones: span(notes), notes: notes.length,
    responseNotes: (tracksByOwner.get(phrase.response_id)?.notes || []).filter(note => note.startBeat >= start && note.startBeat < end).length,
    consequence: phrase.consequence };
});
const sections = plan.sections.map(section => {
  const start = section.start_bar * midi.beatsPerBar, end = (section.start_bar + section.bars) * midi.beatsPerBar;
  return { name: section.name, firstBar: section.start_bar + 1, bars: section.bars,
    timeSeconds: [midi.beatToSeconds(start), midi.beatToSeconds(end)],
    notesByTrack: tracks.map(track => track.notes.filter(note => note.startBeat >= start && note.startBeat < end).length),
    harmony: section.harmonic_events.map(event => ({ beat: event.absolute_beat, chord: event.chord_id })) };
});
const harmonyEvents = plan.sections.flatMap(section => section.harmonic_events)
  .sort((a, b) => a.absolute_beat - b.absolute_beat);
const sustainedTensions = [];
for (let i = 0; i < all.length; i++) for (let j = i + 1; j < all.length; j++) {
  const left = all[i], right = all[j];
  if (right.startBeat >= left.startBeat + left.durationBeats) break;
  const beat = Math.max(left.startBeat, right.startBeat);
  const overlap = Math.min(left.startBeat + left.durationBeats, right.startBeat + right.durationBeats) - beat;
  const interval = Math.abs(left.pitch - right.pitch) % 12;
  if (overlap < .5 - .000001 || ![1, 6, 11].includes(interval)) continue;
  const event = harmonyEvents.filter(event => event.absolute_beat <= beat + .000001).at(-1);
  const chord = plan.chord_palette.find(chord => chord.id === event?.chord_id);
  sustainedTensions.push({ beat, bar: Math.floor(beat / midi.beatsPerBar) + 1,
    overlapBeats: beatRound(overlap), intervalClass: interval,
    owners: [left.owner, right.owner], pitches: [left.pitch, right.pitch], chordId: chord?.id || null,
    bothDeclaredChordTones: Boolean(chord && chord.pitch_classes.includes(left.pitch % 12) && chord.pitch_classes.includes(right.pitch % 12)) });
}
sustainedTensions.sort((a, b) => b.overlapBeats - a.overlapBeats || a.beat - b.beat);
const lastOff = all.reduce((latest, note) => Math.max(latest, note.startBeat + note.durationBeats), 0);
const report = { schemaVersion: 1, advisoryOnly: true,
  method: 'Actual exported MIDI. Two-bar shapes ignore pitch and velocity. Connected intervals require successive monophonic attacks no more than one bar apart. No musical quality score is inferred.',
  jobId: path.basename(directory), title: manifest.title, prompt: request.prompt, seed: request.seed,
  requestedSeconds: request.duration_seconds, midiSeconds: midi.beatToSeconds(midi.lengthBeats),
  trackCount: tracks.length, totalNotes: all.length, noteProvenance: manifest.note_provenance,
  unterminatedNotes: all.filter(note => note.unterminated).length,
  lastNoteOffSeconds: midi.beatToSeconds(lastOff), terminalSilenceSeconds: midi.beatToSeconds(midi.lengthBeats) - midi.beatToSeconds(lastOff),
  samePitchRapidAttacksWithinTrack: pairRepeats(all, true),
  samePitchRapidAttacksAcrossTracks: pairRepeats(all, false),
  sustainedIntervalEvidence: { advisoryOnly: true,
    method: 'Overlapping semitone/seventh/tritone pitch classes lasting at least half a beat; declared chord tones and prepared resolutions can be intentional. These pairs are not automatically errors.',
    count: sustainedTensions.length, examples: sustainedTensions.slice(0, 40) },
  tracks: trackEvidence, phrases, sections };
const outputIndex = process.argv.indexOf('--output');
if (outputIndex >= 0) {
  if (!process.argv[outputIndex + 1]) throw new Error('--output requires a filename');
  fs.writeFileSync(path.resolve(process.argv[outputIndex + 1]), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({ title: report.title, output: path.resolve(process.argv[outputIndex + 1]), notes: report.totalNotes, tracks: report.trackCount }));
} else console.log(JSON.stringify(report, null, 2));
