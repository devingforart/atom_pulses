import { readFileSync } from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { parseMidi } from '../web/shared/midi.mjs';

// Descriptive evidence only: no grades, quantisation, rewriting or MIDI writes.
export function measureGroove(tracks, beatsPerBar = 4) {
  const byPitch = new Map(), nearAttacks = [];
  for (const track of tracks) for (const note of track.notes) {
    const group = byPitch.get(note.pitch) || [];
    group.push({ owner: track.name, beat: note.startBeat });
    byPitch.set(note.pitch, group);
  }
  for (const [pitch, group] of byPitch) {
    group.sort((a, b) => a.beat - b.beat);
    for (let i = 0; i < group.length; i++) for (let j = i + 1; j < group.length; j++) {
      const gap = group[j].beat - group[i].beat;
      if (gap >= .5) break;
      if (gap > .001 && group[i].owner !== group[j].owner)
        nearAttacks.push({ owners: [group[i].owner, group[j].owner], pitch,
          beats: [group[i].beat, group[j].beat], gap });
    }
  }
  const onsets = tracks.map(track => ({ name: track.name,
    durations: track.notes.map(note => note.durationBeats).sort((a, b) => a - b),
    velocities: track.notes.map(note => note.velocity).filter(Number.isFinite),
    attacks: [...new Set(track.notes.map(note => note.startBeat))].sort((a, b) => a - b) }));
  return {
    interpretation: 'Attack positions and recurrence are evidence, not proof of an audible club groove.',
    tracks: onsets.map(track => {
      const phases = new Map();
      for (const beat of track.attacks) {
        const phase = +(beat % beatsPerBar).toFixed(4);
        phases.set(phase, (phases.get(phase) || 0) + 1);
      }
      const cycles = [1, 2, 4].map(bars => {
        const length = bars * beatsPerBar, patterns = new Map();
        const last = Math.max(0, ...track.attacks);
        for (let start = 0; start <= last; start += length) {
          const offsets = track.attacks.filter(beat => beat >= start && beat < start + length)
            .map(beat => +(beat - start).toFixed(4));
          if (!offsets.length) continue;
          const key = JSON.stringify(offsets);
          patterns.set(key, (patterns.get(key) || 0) + 1);
        }
        return { bars, distinctAttackPatterns: patterns.size,
          mostFrequent: [...patterns].sort((a, b) => b[1] - a[1]).slice(0, 3)
            .map(([positions, appearances]) => ({ positions: JSON.parse(positions), appearances })) };
      });
      return { name: track.name, attacks: track.attacks.length,
        soundingDurationBeats: track.durations.length ? {
          minimum: track.durations[0], median: track.durations[Math.floor(track.durations.length / 2)],
          maximum: track.durations.at(-1) } : null,
        velocityRange: track.velocities.length ? [Math.min(...track.velocities), Math.max(...track.velocities)] : [],
        beatPhaseHistogram: [...phases].sort((a, b) => a[0] - b[0]), cycles };
    }),
    closeSamePitchCrossVoiceAttacks: nearAttacks
  };
}

export function auditGrooveDirectory(directory) {
  const manifest = JSON.parse(readFileSync(path.join(directory, 'manifest.json'), 'utf8'));
  const plan = JSON.parse(readFileSync(path.join(directory, 'composition-plan.json'), 'utf8'));
  return measureGroove(manifest.tracks.map(track => {
    if (path.basename(track.filename) !== track.filename) throw new Error('Invalid MIDI path');
    return { name: track.name, notes: parseMidi(readFileSync(path.join(directory, track.filename)))
      .tracks.flatMap(item => item.notes) };
  }), plan.beats_per_bar);
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href)
  console.log(JSON.stringify(auditGrooveDirectory(path.resolve(process.argv[2])), null, 2));
