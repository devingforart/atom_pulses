// Descriptive evidence from actual MIDI, never a generator or a quality gate.
const median = values => {
  const sorted = [...values].sort((a, b) => a - b);
  return sorted.length ? (sorted[Math.floor((sorted.length - 1) / 2)] +
    sorted[Math.floor(sorted.length / 2)]) / 2 : null;
};
const round = value => Math.round(value * 10000) / 10000;

export function measureTrackDelivery(track, midi) {
  const notes = [...track.notes].sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch);
  const groups = [];
  for (const note of notes) {
    const last = groups.at(-1);
    if (last && Math.abs(last.beat - note.startBeat) < .000001) last.notes.push(note);
    else groups.push({ beat: note.startBeat, notes: [note] });
  }
  const durations = notes.map(note => midi.beatToSeconds(note.startBeat + note.durationBeats) -
    midi.beatToSeconds(note.startBeat));
  let soundingSeconds = 0, mergedStart = null, mergedEnd = null;
  for (const note of notes) {
    const start = midi.beatToSeconds(note.startBeat);
    const end = midi.beatToSeconds(note.startBeat + note.durationBeats);
    if (mergedStart === null) { mergedStart = start; mergedEnd = end; }
    else if (start <= mergedEnd) mergedEnd = Math.max(mergedEnd, end);
    else { soundingSeconds += mergedEnd - mergedStart; mergedStart = start; mergedEnd = end; }
  }
  if (mergedStart !== null) soundingSeconds += mergedEnd - mergedStart;
  const links = [];
  for (let index = 1; index < groups.length; index++) {
    const left = groups[index - 1], right = groups[index];
    if (left.notes.length !== 1 || right.notes.length !== 1) continue;
    const note = left.notes[0], next = right.notes[0];
    const intervalSeconds = midi.beatToSeconds(next.startBeat) - midi.beatToSeconds(note.startBeat);
    const durationSeconds = midi.beatToSeconds(note.startBeat + note.durationBeats) - midi.beatToSeconds(note.startBeat);
    links.push({ beat: note.startBeat, nextBeat: next.startBeat,
      pitches: [note.pitch, next.pitch], onsetIntervalSeconds: round(intervalSeconds),
      soundingSeconds: round(durationSeconds),
      gapSeconds: round(intervalSeconds - durationSeconds),
      articulationRatio: round(durationSeconds / intervalSeconds) });
  }
  // Thresholds below select listening examples, not unacceptable music. A breath,
  // staccato line or virtuosic run can intentionally match these descriptions.
  const separated = links.filter(link => link.onsetIntervalSeconds <= 1 &&
    link.articulationRatio < .6 && link.gapSeconds > .08);
  const rapidRuns = [];
  let run = [];
  const finish = () => {
    if (run.length >= 2) rapidRuns.push({ startBeat: run[0].beat,
      endBeat: run.at(-1).nextBeat, attacks: run.length + 1 });
    run = [];
  };
  for (const link of links) {
    if (link.onsetIntervalSeconds > .25 + .00001) { finish(); continue; }
    if (run.length && Math.abs(run.at(-1).nextBeat - link.beat) > .00001) finish();
    run.push(link);
  }
  finish();
  return { notes: notes.length, medianDurationSeconds: median(durations),
    soundingSeconds: round(soundingSeconds),
    under200ms: durations.filter(value => value < .2 - .00001).length,
    under300ms: durations.filter(value => value < .3 - .00001).length,
    monophonicTransitions: links.length,
    medianArticulationRatio: median(links.filter(link => link.onsetIntervalSeconds <= 1)
      .map(link => link.articulationRatio)),
    separatedShortLinks: { count: separated.length, examples: separated.slice(0, 16) },
    rapidRuns: { count: rapidRuns.length, examples: rapidRuns.slice(0, 16) } };
}

export function analyzeMelodicDelivery(midi, plan, architecture) {
  const byOwner = new Map((plan.cast || []).map(part => [part.id,
    midi.tracks.find(track => track.name === part.name || track.name === `PULSO ${part.name}`)]));
  const phrases = (architecture?.phrases || []).map((phrase, index) => {
    const section = plan.sections[phrase.section_index];
    if (!section) throw new Error('Phrase refers to an unknown section');
    const start = (section.start_bar + phrase.local_start_bar) * midi.beatsPerBar;
    const end = start + phrase.bars * midi.beatsPerBar;
    const notes = (byOwner.get(phrase.speaker_id)?.notes || []).filter(note =>
      note.startBeat < end && note.startBeat + note.durationBeats > start);
    const anchors = (phrase.delivery?.structural_tones || []).map(tone => {
      const beat = start + tone.beat;
      const matches = notes.filter(note => note.pitch === tone.pitch &&
        Math.abs(note.startBeat - beat) < .001);
      return { beat, pitch: tone.pitch, intendedDuration: tone.duration,
        purpose: tone.purpose, actualDurations: matches.map(note => note.durationBeats) };
    });
    return { index, startBeat: start, endBeat: end, speaker: phrase.speaker_id,
      respondent: phrase.response_id, delivery: phrase.delivery || null,
      observed: measureTrackDelivery({ notes }, midi), bearingTones: anchors };
  });
  return { schemaVersion: 1, advisoryOnly: true,
    method: 'MIDI durations converted through its tempo map. Short notes, detached links and rapid runs are listening evidence, not errors. No numeric value establishes narrative quality.',
    tracks: (plan.cast || []).flatMap(part => {
      const track = byOwner.get(part.id);
      return track ? [{ id: part.id, name: track.name, ...measureTrackDelivery(track, midi) }] : [];
    }), phrases };
}
