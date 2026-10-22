import { readFileSync } from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { parseMidi } from '../web/shared/midi.mjs';

// Read-only: verifies the AI's own timing promises, not musical taste.
export function summarizeTiming(notes, layer, core = false) {
  const attacks = [...new Set(notes.map(n => n.startBeat))].sort((a, b) => a - b);
  const bound = core || layer?.timing_mode === 'cycle_bound';
  const cycle = layer?.cycle_beats, origin = layer?.cycle_origin_beat;
  const mismatch = attacks.filter(beat => {
    if (!bound) return false;
    if (!(cycle > 0) || beat < origin - .001) return true;
    const phase = ((beat - origin) % cycle + cycle) % cycle;
    return !layer.rhythm_cell.some(offset => Math.abs(phase - offset) < .001);
  });
  const positions = { onBeat: 0, eighthOffbeat: 0, sixteenth: 0, other: 0 };
  for (const beat of attacks) {
    const phase = ((beat % 1) + 1) % 1;
    if (phase < .001 || phase > .999) positions.onBeat++;
    else if (Math.abs(phase - .5) < .001) positions.eighthOffbeat++;
    else if (Math.abs(phase - .25) < .001 || Math.abs(phase - .75) < .001) positions.sixteenth++;
    else positions.other++;
  }
  return { timingMode: bound ? 'cycle_bound' : 'phrase_flexible_or_legacy',
    attacks: attacks.length, positions, contractMismatches: mismatch.length,
    mismatchExamples: mismatch.slice(0, 8) };
}

export function auditTiming(directory) {
  const read = file => JSON.parse(readFileSync(path.join(directory, file), 'utf8'));
  const manifest = read('manifest.json'), plan = read('composition-plan.json');
  const production = read('club-production-plan.json');
  const tracks = manifest.tracks.map(track => {
    if (path.basename(track.filename) !== track.filename) throw new Error('Invalid MIDI filename');
    const owner = plan.cast.find(p => p.name === track.name);
    if (!owner) throw new Error(`Unknown MIDI owner: ${track.name}`);
    const layer = production.layers.find(p => p.instrument_id === owner.id);
    const notes = parseMidi(readFileSync(path.join(directory, track.filename))).tracks.flatMap(t => t.notes);
    return { id: owner.id, name: track.name,
      ...summarizeTiming(notes, layer, production.groove_core_ids?.includes(owner.id)) };
  });
  return { title: manifest.title, contractMismatches: tracks.reduce((s, t) => s + t.contractMismatches, 0),
    interpretation: 'Subdivisions describe actual attacks, not errors by themselves. Only explicit cyclic commitments are checked.', tracks };
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  if (!process.argv[2]) throw new Error('Usage: node scripts/audit-club-timing.mjs <completed-job-directory>');
  console.log(JSON.stringify(auditTiming(path.resolve(process.argv[2])), null, 2));
}
