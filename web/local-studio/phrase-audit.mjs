import { readFileSync } from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../shared/midi.mjs';

// An objective trace, not a numerical verdict on expression or musicality.
export function analyzePhraseArchitecture(architecture, plan, midi) {
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

export function analyzePhraseArchitectureFiles(directory) {
  const read = name => JSON.parse(readFileSync(path.join(directory, name), 'utf8'));
  return analyzePhraseArchitecture(read('phrase-architecture.json'),
    read('composition-plan.json'), parseMidi(readFileSync(path.join(directory, 'full-song.mid'))));
}
