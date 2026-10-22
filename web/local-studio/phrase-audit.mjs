import { readFileSync } from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../shared/midi.mjs';

// An objective trace, not a numerical verdict on expression or musicality.
export function analyzePhraseArchitecture(architecture, plan, midi) {
  if (architecture?.kind === 'club_production_v1') return analyzeClubProduction(architecture, plan, midi);
  if (!Array.isArray(architecture?.phrases) || !Array.isArray(plan?.sections) ||
      !Array.isArray(plan?.cast) || !Array.isArray(midi?.tracks))
    throw new Error('Incomplete phrase-plan audit input');
  const partById = new Map(plan.cast.map((part, index) => [part.id, midi.tracks[index + 1]]));
  const phrases = architecture.phrases.map((phrase, index) => {
    const section = plan.sections[phrase.section_index];
    if (!section) throw new Error('Unknown phrase section');
    const start = (section.start_bar + phrase.local_start_bar) * midi.beatsPerBar;
    const end = start + phrase.bars * midi.beatsPerBar;
    const notes = (partById.get(phrase.speaker_id)?.notes || []).filter(note =>
      note.startBeat >= start && note.startBeat < end);
    const responses = (partById.get(phrase.response_id)?.notes || []).filter(note =>
      note.startBeat >= start && note.startBeat < end);
    return { index, sectionIndex: phrase.section_index,
      absoluteStartBar: section.start_bar + phrase.local_start_bar, bars: phrase.bars,
      speakerId: phrase.speaker_id, responseId: phrase.response_id,
      speakerNotes: notes.length, responseNotes: responses.length,
      registerAdherence: notes.length ? notes.filter(note =>
        note.pitch >= phrase.register_low && note.pitch <= phrase.register_high).length /
        notes.length : null,
      observedPitchRange: notes.length ? [Math.min(...notes.map(note => note.pitch)),
        Math.max(...notes.map(note => note.pitch))] : null,
      // Contour, dialogue and emotional consequence still require listening.
      needsListening: true };
  });
  return { schemaVersion: 1, advisoryOnly: true, phrases,
    silentSpeakerPhrases: phrases.filter(phrase => phrase.speakerNotes === 0).length,
    plannedResponsesWithoutNotes: phrases.filter(phrase => phrase.responseId &&
      phrase.responseNotes === 0).length };
}

export function analyzeClubProduction(architecture, plan, midi) {
  if (!Array.isArray(architecture?.layers) || !Array.isArray(plan?.cast) ||
      !Array.isArray(plan?.sections) || !Array.isArray(midi?.tracks))
    throw new Error('Incomplete Club production audit input');
  const issues = [];
  const layers = architecture.layers.map(layer => {
    const owner = plan.cast.findIndex(part => part.id === layer.instrument_id);
    if (owner < 0) throw new Error('Unknown Club production owner');
    const notes = midi.tracks[owner + 1]?.notes || [];
    const core = (architecture.groove_core_ids || []).includes(layer.instrument_id);
    const phaseMismatchAttacks = core ? [...new Set(notes.map(note => note.startBeat))].filter(beat => {
      const phase = ((beat - layer.cycle_origin_beat) % layer.cycle_beats + layer.cycle_beats) % layer.cycle_beats;
      return beat < layer.cycle_origin_beat - .001 ||
        !layer.rhythm_cell.some(offset => Math.abs(phase - offset) < .001);
    }) : [];
    if (phaseMismatchAttacks.length) issues.push({ owner: layer.instrument_id,
      kind: 'core_phase_mismatch', attacks: phaseMismatchAttacks });
    const states = layer.states.map(state => {
      const section = plan.sections[state.section_index];
      if (!section) throw new Error('Unknown Club section');
      const begin = section.start_bar * midi.beatsPerBar;
      const end = begin + section.bars * midi.beatsPerBar;
      const sounding = notes.filter(note => note.startBeat < end && note.startBeat + note.durationBeats > begin);
      const spans = sounding.map(note => [Math.max(begin, note.startBeat),
        Math.min(end, note.startBeat + note.durationBeats)]).sort((a, b) => a[0] - b[0]);
      let coverage = 0, previousEnd = begin, polyphonic = false;
      for (const [start, stop] of spans) {
        if (start < previousEnd - .001) polyphonic = true;
        coverage += Math.max(0, stop - Math.max(start, previousEnd));
        previousEnd = Math.max(previousEnd, stop);
      }
      const fraction = coverage / (end - begin);
      if (state.mode === 'absent' && sounding.length)
        issues.push({ owner: layer.instrument_id, section: state.section_index, kind: 'unexpected_activity' });
      if (state.mode !== 'absent') {
        if (coverage + .25 < Math.floor(state.minimum_sounding_fraction * (end - begin) * 4 + .001) / 4)
          issues.push({ owner: layer.instrument_id, section: state.section_index, kind: 'under_declared_coverage' });
        if (layer.polyphonic && !polyphonic && sounding.length)
          issues.push({ owner: layer.instrument_id, section: state.section_index, kind: 'missing_declared_polyphony' });
      }
      return { sectionIndex: state.section_index, mode: state.mode, notes: sounding.length,
        actualSoundingFraction: fraction, declaredMinimum: state.minimum_sounding_fraction,
        observedPolyphony: polyphonic };
    });
    return { owner: layer.instrument_id, role: layer.production_role, cycleBeats: layer.cycle_beats,
      core, phaseMismatchAttacks, notes: notes.length, states };
  });
  return { schemaVersion: 1, kind: 'club_production_v1', advisoryOnly: true,
    format: architecture.format, layers, issues, phrases: [], needsListening: true };
}

export function analyzePhraseArchitectureFiles(directory) {
  const read = name => JSON.parse(readFileSync(path.join(directory, name), 'utf8'));
  return analyzePhraseArchitecture(read('phrase-architecture.json'),
    read('composition-plan.json'), parseMidi(readFileSync(path.join(directory, 'full-song.mid'))));
}
