import { readFileSync } from 'node:fs';
import path from 'node:path';
import { pathToFileURL } from 'node:url';
import { parseMidi } from '../web/shared/midi.mjs';

// Read-only evidence of the promised rhythmic body. No scores, MIDI edits,
// generated patterns or thresholds that claim to certify musical quality.
export function measureLane(notes, states, sections, cycleBeats, referenceAttacks) {
  const attacks = [...new Set(notes.map(n => n.startBeat))].sort((a, b) => a - b);
  const cycles = [];
  const activeSpans = states.filter(s => s.mode === 'active' || s.mode === 'sustain').map(s => {
    const section = sections[s.section_index];
    if (!section) return null;
    return { start: section.start_bar * 4 + (s.entry_beat ?? 0),
      end: section.start_bar * 4 + (s.release_beat ?? section.bars * 4), mode: s.mode };
  }).filter(Boolean);
  let available = 0, sounding = 0, largestGap = 0;
  for (const span of activeSpans) {
    available += Math.max(0, span.end - span.start);
    const intervals = notes.filter(n => n.startBeat < span.end && n.startBeat + n.durationBeats > span.start)
      .map(n => [Math.max(span.start, n.startBeat), Math.min(span.end, n.startBeat + n.durationBeats)])
      .sort((a, b) => a[0] - b[0]);
    let cursor = span.start;
    for (const [begin, end] of intervals) {
      largestGap = Math.max(largestGap, begin - cursor);
      sounding += Math.max(0, end - Math.max(begin, cursor));
      cursor = Math.max(cursor, end);
    }
    largestGap = Math.max(largestGap, span.end - cursor);
    if (!(cycleBeats > 0)) continue;
    for (let start = Math.ceil(span.start / cycleBeats) * cycleBeats;
      start + cycleBeats <= span.end + .001; start += cycleBeats) {
      const actual = attacks.filter(b => b >= start && b < start + cycleBeats)
        .map(b => +(b - start).toFixed(4));
      cycles.push({ startBar: start / 4 + 1, attacks: actual.length,
        referenceHits: referenceAttacks.filter(b => actual.some(a => Math.abs(a - b) < .001)).length });
    }
  }
  const mean = values => values.length ? values.reduce((a, b) => a + b, 0) / values.length : null;
  return { noteCount: notes.length, distinctAttacks: attacks.length,
    activeSoundingFraction: available ? +(sounding / available).toFixed(3) : null,
    largestActiveSilenceBeats: +largestGap.toFixed(3),
    referenceAttacksPerCycle: referenceAttacks.length,
    measuredCompleteCycles: cycles.length,
    meanActualAttacksPerCycle: mean(cycles.map(c => c.attacks)),
    meanReferenceAttackRetention: referenceAttacks.length
      ? mean(cycles.map(c => c.referenceHits / referenceAttacks.length)) : null, cycles };
}

export function auditNucleusDirectory(directory) {
  const read = name => JSON.parse(readFileSync(path.join(directory, name), 'utf8'));
  const manifest = read('manifest.json'), composition = read('composition-plan.json');
  const production = read('club-production-plan.json');
  if (composition.beats_per_bar !== 4) throw new Error('This Club diagnostic currently measures 4/4 scores only');
  return { title: manifest.title, bars: manifest.bars, key: manifest.key,
    interpretation: 'Descriptive MIDI evidence only. Recurrence, density and consonance do not certify a good groove.',
    concreteAiNucleus: Boolean(production.tonal_nucleus),
    sections: composition.sections.map(s => ({ name: s.name, startBar: s.start_bar + 1, bars: s.bars })),
    tracks: manifest.tracks.map(track => {
      if (path.basename(track.filename) !== track.filename) throw new Error('Invalid MIDI path');
      const owner = composition.cast.find(p => p.name === track.name);
      const layer = production.layers.find(l => l.instrument_id === owner?.id);
      if (!layer) throw new Error(`Missing declared layer for ${track.name}`);
      const reference = production.tonal_nucleus?.lanes.find(l => l.instrument_id === owner?.id);
      const cycle = reference ? production.tonal_nucleus.cycle_beats : layer.cycle_beats;
      const positions = [...new Set(reference ? reference.notes.map(n => n.beat) : layer.rhythm_cell)];
      const notes = parseMidi(readFileSync(path.join(directory, track.filename))).tracks.flatMap(t => t.notes);
      return { name: track.name, id: owner.id, timingMode: layer.timing_mode,
        ...measureLane(notes, layer.states, composition.sections, cycle, positions) };
    }) };
}

if (process.argv[1] && import.meta.url === pathToFileURL(path.resolve(process.argv[1])).href)
  console.log(JSON.stringify(auditNucleusDirectory(path.resolve(process.argv[2])), null, 2));
