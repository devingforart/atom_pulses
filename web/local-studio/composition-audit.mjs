// Read-only MIDI analysis for the local studio. Exact eight-bar repetition is
// measured from the published notes, independently of the AI provenance tags.
import { readFileSync } from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../shared/midi.mjs';

const rounded = value => Math.round(value * 1000) / 1000;

export function analyzeComposition(directory, manifest) {
  const fullFile = manifest.fullFile || 'full-song.mid';
  if (!/^[a-zA-Z0-9._-]+\.mid$/i.test(fullFile))
    throw new Error('Unexpected full MIDI filename in manifest');
  const beatsPerBar = parseMidi(readFileSync(path.join(directory, fullFile))).beatsPerBar;
  const windowBeats = 8 * beatsPerBar;
  const totalBars = Number(manifest.bars) || 0;
  const tracks = (manifest.tracks || []).map((track, index) => {
    if (!/^track-[0-9]+\.mid$/i.test(track.filename || ''))
      throw new Error('Unexpected MIDI track filename in manifest');
    const file = path.join(directory, track.filename);
    const notes = parseMidi(readFileSync(file)).tracks.flatMap(item => item.notes);
    const sidecar = JSON.parse(readFileSync(file.replace(/\.mid$/i, '.pulso.json'), 'utf8'));
    const part = sidecar.parts?.[0] || {};
    const windows = new Map();
    const sustainedBars = new Set();
    for (const note of notes) {
      const window = Math.floor((note.startBeat + 1e-8) / windowBeats);
      const relativeBeat = rounded(note.startBeat - window * windowBeats);
      if (!windows.has(window)) windows.set(window, []);
      windows.get(window).push(JSON.stringify([
        relativeBeat, note.pitch, rounded(note.durationBeats),
      ]));
      const firstBar = Math.max(0, Math.floor(note.startBeat / beatsPerBar));
      const lastBar = Math.min(totalBars - 1,
        Math.floor((note.startBeat + note.durationBeats - 0.001) / beatsPerBar));
      for (let bar = firstBar; bar <= lastBar; bar++) {
        const barStart = bar * beatsPerBar;
        const overlap = Math.min(note.startBeat + note.durationBeats,
          barStart + beatsPerBar) - Math.max(note.startBeat, barStart);
        if (overlap + 0.001 >= Math.min(2, beatsPerBar * 0.5)) sustainedBars.add(bar);
      }
    }
    const seen = new Set();
    let exactRepeatedEightBarNotes = 0;
    for (const events of windows.values()) {
      events.sort();
      const signature = events.join('|');
      if (seen.has(signature)) exactRepeatedEightBarNotes += events.length;
      else seen.add(signature);
    }
    return {
      id: index + 1, name: track.name, catalogId: part.catalog_id || '',
      relationship: part.line_relationship || '', contentLaneId: part.content_lane_id || '',
      sourceVoice: part.source_voice || '', orchestralFunction: part.orchestral_function || '',
      noteCount: notes.length, activeEightBarWindows: windows.size,
      uniqueEightBarWindows: seen.size, exactRepeatedEightBarNotes,
      sustainedBarCount: sustainedBars.size, sustainedBars: [...sustainedBars].sort((a, b) => a - b),
      notes,
    };
  });
  const totalNotes = tracks.reduce((sum, track) => sum + track.noteCount, 0);
  const exactRepeatedEightBarNotes = tracks.reduce(
    (sum, track) => sum + track.exactRepeatedEightBarNotes, 0);
  const eventOwners = new Map();
  for (const track of tracks) for (const note of track.notes) {
    const key = `${rounded(note.startBeat)}:${note.pitch}:${rounded(note.durationBeats)}`;
    if (!eventOwners.has(key)) eventOwners.set(key, new Set());
    eventOwners.get(key).add(track.id);
  }
  const crossTrackExactNoteInstances = [...eventOwners.values()].reduce(
    (sum, owners) => sum + (owners.size > 1 ? owners.size : 0), 0);
  const provenance = manifest.note_provenance || null;
  const provenanceTotal = provenance && Object.values(provenance).reduce(
    (sum, count) => sum + (Number(count) || 0), 0);
  const harmonicBodies = tracks.filter(track =>
    ['harmonic_foundation', 'harmonic_upper', 'atmosphere'].includes(track.sourceVoice) &&
    ['foundation', 'body'].includes(track.orchestralFunction) &&
    !['call_response', 'relay', 'timbral_handoff'].includes(track.relationship) &&
    !/(sub|bass|kick|arpeggio|arp|sequence)/i.test(`${track.name} ${track.catalogId}`));
  const sustainedLayerCountByBar = Array.from({ length: totalBars }, (_, bar) =>
    harmonicBodies.filter(track => track.sustainedBars.includes(bar)).length);
  return {
    schema_version: 1,
    method: 'Exact relative note onset/pitch/duration in each eight-bar window; velocity is ignored.',
    totalNotes, eightBarWindowBeats: windowBeats, exactRepeatedEightBarNotes,
    exactRepeatedEightBarRatio: totalNotes ? exactRepeatedEightBarNotes / totalNotes : 0,
    crossTrackExactNoteInstances,
    crossTrackExactNoteRatio: totalNotes ? crossTrackExactNoteInstances / totalNotes : 0,
    noteProvenance: provenanceTotal === totalNotes ? provenance : null,
    noteProvenanceAvailable: provenanceTotal === totalNotes,
    sustainedHarmonicBodies: harmonicBodies.map(track => track.id),
    sustainedHarmonicLayersByBar: sustainedLayerCountByBar,
    twoLayerSustainedBarRatio: totalBars ?
      sustainedLayerCountByBar.filter(count => count >= 2).length / totalBars : 0,
    tracks: tracks.map(({ notes, sustainedBars, ...track }) => track),
  };
}
