import { readFileSync } from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { parseMidi } from '../web/shared/midi.mjs';

// Independent read-only audit of the exported notes, not blueprint claims.
// These observations are not aesthetic grades and never change the score.
export function auditClubScore(directory) {
  const read = name => JSON.parse(readFileSync(path.join(directory, name), 'utf8'));
  const manifest = read('manifest.json'), plan = read('composition-plan.json');
  const beatsPerBar = plan.beats_per_bar, bars = plan.total_bars;
  const tracks = manifest.tracks.map(track => {
    if (path.basename(track.filename) !== track.filename) throw new Error('Invalid MIDI path');
    const score = parseMidi(readFileSync(path.join(directory, track.filename)));
    const notes = score.tracks.flatMap(item => item.notes);
    const sounding = new Set();
    for (const note of notes)
      for (let bar = Math.floor(note.startBeat / beatsPerBar);
        bar < Math.min(bars, Math.ceil((note.startBeat + note.durationBeats) / beatsPerBar)); bar++) sounding.add(bar);
    const fingerprints = new Map();
    const phraseBeats = 4 * beatsPerBar;
    for (let start = 0; start + phraseBeats <= bars * beatsPerBar; start += phraseBeats) {
      const events = notes.filter(note => note.startBeat >= start && note.startBeat < start + phraseBeats);
      if (!events.length) continue;
      const key = events.map(note => [note.pitch, +(note.startBeat - start).toFixed(4),
        +note.durationBeats.toFixed(4)]).sort((a, b) => a[1] - b[1] || a[0] - b[0]);
      const signature = JSON.stringify(key);
      fingerprints.set(signature, (fingerprints.get(signature) || 0) + 1);
    }
    const cycles = [...fingerprints.values()];
    const attacks = new Map();
    for (const note of notes) attacks.set(note.startBeat, (attacks.get(note.startBeat) || 0) + 1);
    const grouped = new Map();
    let samePitchOverlaps = 0;
    for (const note of [...notes].sort((a, b) => a.startBeat - b.startBeat)) {
      const previousEnd = grouped.get(note.pitch) ?? -1;
      if (previousEnd > note.startBeat + 0.001) samePitchOverlaps++;
      grouped.set(note.pitch, Math.max(previousEnd, note.startBeat + note.durationBeats));
    }
    return { name: track.name, notes: notes.length, soundingBars: sounding.size,
      occupied: +(sounding.size / bars).toFixed(3),
      pitchRange: notes.length ? [Math.min(...notes.map(n => n.pitch)), Math.max(...notes.map(n => n.pitch))] : [],
      simultaneousAttacks: Math.max(0, ...attacks.values()), samePitchOverlaps,
      fourBarPatterns: fingerprints.size, longestLiteralPatternReturns: Math.max(0, ...cycles),
      literalRecurrence: cycles.length ? +(cycles.filter(n => n > 1).reduce((a, b) => a + b, 0) /
        cycles.reduce((a, b) => a + b, 0)).toFixed(3) : 0,
      offGrid: notes.filter(n => Math.abs(n.startBeat * 4 - Math.round(n.startBeat * 4)) > .001).length,
      notesInternal: notes, soundingInternal: sounding };
  });
  const barActivity = Array.from({ length: bars }, (_, bar) => tracks.filter(t => t.soundingInternal.has(bar)).length);
  const allNotes = tracks.flatMap(t => t.notesInternal);
  const ordered = tracks.flatMap((track, owner) => track.notesInternal.map(note => ({ ...note, owner })))
    .sort((a, b) => a.startBeat - b.startBeat);
  let active = [], sustainedAdjacentPairs = 0, sustainedInternalAdjacentPairs = 0;
  const sustainedAdjacentExamples = [];
  const sustainedInternalAdjacentExamples = [];
  let sustainedCompoundMinorNinthPairs = 0;
  const sustainedCompoundMinorNinthExamples = [];
  for (const note of ordered) {
    active = active.filter(previous => previous.startBeat + previous.durationBeats > note.startBeat);
    for (const previous of active) {
      const interval = Math.abs(previous.pitch - note.pitch);
      if (interval % 12 !== 1) continue;
      const overlap = Math.min(previous.startBeat + previous.durationBeats,
        note.startBeat + note.durationBeats) - note.startBeat;
      if (overlap < 1) continue;
      if (interval > 1) {
        sustainedCompoundMinorNinthPairs++;
        if (sustainedCompoundMinorNinthExamples.length < 16) sustainedCompoundMinorNinthExamples.push({
          bar: Math.floor(note.startBeat / beatsPerBar) + 1, beat: note.startBeat,
          overlapBeats: overlap, pitches: [previous.pitch, note.pitch],
          tracks: [tracks[previous.owner].name, tracks[note.owner].name] });
        continue;
      }
      if (previous.owner === note.owner) {
        sustainedInternalAdjacentPairs++;
        if (sustainedInternalAdjacentExamples.length < 8) sustainedInternalAdjacentExamples.push({
          bar: Math.floor(note.startBeat / beatsPerBar) + 1, beat: note.startBeat,
          overlapBeats: overlap, pitches: [previous.pitch, note.pitch], track: tracks[note.owner].name });
        continue;
      }
      sustainedAdjacentPairs++;
      if (sustainedAdjacentExamples.length < 16) sustainedAdjacentExamples.push({
        bar: Math.floor(note.startBeat / beatsPerBar) + 1, beat: note.startBeat,
        overlapBeats: overlap, pitches: [previous.pitch, note.pitch],
        tracks: [tracks[previous.owner].name, tracks[note.owner].name] });
    }
    active.push(note);
  }
  const ending = allNotes.filter(n => n.startBeat >= (bars - 8) * beatsPerBar);
  const result = { title: manifest.title, behavior: manifest.composition_behavior,
    key: manifest.key, bpm: manifest.bpm, bars, provenance: manifest.note_provenance,
    notes: allNotes.length, silentBars: barActivity.filter(n => n === 0).length,
    sustainedAdjacentPairs, sustainedAdjacentExamples,
    sustainedInternalAdjacentPairs, sustainedInternalAdjacentExamples,
    sustainedCompoundMinorNinthPairs, sustainedCompoundMinorNinthExamples,
    peakConcurrentTracks: Math.max(...barActivity),
    sections: plan.sections.map(section => ({ name: section.name, startBar: section.start_bar + 1,
      bars: section.bars, meanAudibleTracks: +(barActivity.slice(section.start_bar,
        section.start_bar + section.bars).reduce((a, b) => a + b, 0) / section.bars).toFixed(2) })),
    ending: { attacksLastEightBars: ending.length,
      firstAttackBeat: Math.min(bars * beatsPerBar, ...allNotes.map(n => n.startBeat)),
      cyclicGapBeats: Math.max(0, Math.min(bars * beatsPerBar, ...allNotes.map(n => n.startBeat)) +
        bars * beatsPerBar - Math.max(0, ...allNotes.map(n => n.startBeat + n.durationBeats))),
      finalReleaseBeat: Math.max(0,
      ...allNotes.map(n => n.startBeat + n.durationBeats)), lengthBeats: bars * beatsPerBar },
    tracks: tracks.map(({ notesInternal, soundingInternal, ...track }) => track) };
  return result;
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  if (!process.argv[2]) throw new Error('Usage: node scripts/audit-club-score.mjs <job-directory>');
  console.log(JSON.stringify(auditClubScore(path.resolve(process.argv[2])), null, 2));
}
