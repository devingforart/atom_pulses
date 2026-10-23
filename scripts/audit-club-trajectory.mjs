import { readFileSync } from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { parseMidi } from '../web/shared/midi.mjs';

// Evidence for listening, never a note editor or an automatic aesthetic grade.
export function trajectoryEvidence(plan, production, tracks) {
  const barBeats = plan.beats_per_bar, total = plan.total_bars * barBeats;
  const owners = tracks.map(track => {
    const cast = plan.cast.find(part => part.name === track.name);
    if (!cast) throw new Error(`Unknown owner: ${track.name}`);
    const layer = production.layers.find(part => part.instrument_id === cast.id);
    let violations = 0;
    for (const note of track.notes) {
      const sectionIndex = plan.sections.findIndex(s => note.startBeat >= s.start_bar * barBeats &&
        note.startBeat < (s.start_bar + s.bars) * barBeats);
      const section = plan.sections[sectionIndex];
      const state = layer?.states.find(s => s.section_index === sectionIndex);
      if (!section) { violations++; continue; }
      const start = section.start_bar * barBeats;
      const entry = start + (state?.entry_beat ?? 0);
      const release = start + (state?.release_beat ?? section.bars * barBeats);
      if (state?.mode === 'absent' || note.startBeat < entry - .001 ||
          note.startBeat + note.durationBeats > release + .001) violations++;
    }
    return { name: track.name, notes: track.notes.length,
      firstBeat: track.notes.length ? Math.min(...track.notes.map(n => n.startBeat)) : null,
      finalRelease: track.notes.length ? Math.max(...track.notes.map(n => n.startBeat + n.durationBeats)) : null,
      activityContractViolations: violations };
  });
  const bars = Array.from({ length: plan.total_bars }, (_, bar) => {
    const start = bar * barBeats, end = start + barBeats;
    const attacks = tracks.flatMap(t => t.notes).filter(n => n.startBeat >= start && n.startBeat < end);
    return { bar: bar + 1, notesAttacked: attacks.length,
      soundingVoices: tracks.filter(t => t.notes.some(n => n.startBeat < end && n.startBeat + n.durationBeats > start)).length,
      meanVelocity: attacks.length ? +(attacks.reduce((s, n) => s + n.velocity, 0) / attacks.length).toFixed(1) : null };
  });
  return { owners, bars, opening: bars.slice(0, 8), ending: bars.slice(-8),
    voicesAtFirstInstant: tracks.filter(t => t.notes.some(n => n.startBeat === 0)).length,
    voicesInFinalBar: bars.at(-1)?.soundingVoices ?? 0,
    finalRelease: Math.max(0, ...owners.map(o => o.finalRelease ?? 0)), totalBeats: total,
    activityContractViolations: owners.reduce((s, o) => s + o.activityContractViolations, 0),
    interpretation: 'Activity and release evidence only. Sparse entrances or endings do not establish musical quality; listen to phrase completion and compare the source.' };
}

export function auditTrajectory(directory) {
  const read = name => JSON.parse(readFileSync(path.join(directory, name), 'utf8'));
  const manifest = read('manifest.json'), plan = read('composition-plan.json');
  const production = read('club-production-plan.json');
  const tracks = manifest.tracks.map(t => {
    if (path.basename(t.filename) !== t.filename) throw new Error('Invalid MIDI filename');
    return { name: t.name, notes: parseMidi(readFileSync(path.join(directory, t.filename))).tracks.flatMap(t => t.notes) };
  });
  return { title: manifest.title, ...trajectoryEvidence(plan, production, tracks) };
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href) {
  if (!process.argv[2]) throw new Error('Usage: node scripts/audit-club-trajectory.mjs <job-directory>');
  console.log(JSON.stringify(auditTrajectory(path.resolve(process.argv[2])), null, 2));
}
