// Read-only diagnostic for a local-studio checkpoint. The OpenAI GET retrieves
// the already-completed macro response; it does not create a new generation.
import { readFileSync, readdirSync } from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../web/shared/midi.mjs';
import { jobDirectory } from '../web/local-studio/storage.mjs';

const id = process.argv[2];
if (!id) throw new Error('Usage: node tools/audit-provisional-midi.mjs <job-id>');
const directory = jobDirectory(id);
const events = readFileSync(path.join(directory, 'api-events.jsonl'), 'utf8')
  .trim().split(/\r?\n/).filter(Boolean).map(line => JSON.parse(line));
const macroId = events.findLast(event => event.phase === 'pulso_song_macro' &&
  event.remote_status === 'completed')?.response_id;
if (!macroId) throw new Error('This job has no completed macro response');
if (!process.env.OPENAI_API_KEY) throw new Error('OPENAI_API_KEY is not configured');
const response = await fetch(`https://api.openai.com/v1/responses/${macroId}`, {
  headers: { Authorization: `Bearer ${process.env.OPENAI_API_KEY}` },
});
if (!response.ok) throw new Error(`Could not retrieve saved macro: HTTP ${response.status}`);
const saved = await response.json();
const text = saved.output?.flatMap(item => item.content || [])
  .filter(item => item.type === 'output_text').map(item => item.text).join('') || '';
const plan = JSON.parse(text);
const latest = readdirSync(directory).filter(name => /^partial-\d+\.mid$/.test(name))
  .sort().at(-1);
if (!latest) throw new Error('This job has no provisional MIDI');
const midi = parseMidi(readFileSync(path.join(directory, latest)));
const chords = new Map(plan.chord_palette.map(chord => [chord.id, chord]));
const sections = [];
let startBar = 0;
for (const section of plan.sections) {
  sections.push({ ...section, startBeat: startBar * 4, endBeat: (startBar + section.bars) * 4 });
  startBar += section.bars;
}
const result = midi.tracks.map((track, index) => {
  const outside = [];
  for (const note of track.notes) {
    const section = sections.find(section => note.startBeat >= section.startBeat &&
      note.startBeat < section.endBeat);
    if (!section) continue;
    const localBeat = note.startBeat - section.startBeat;
    const event = section.harmonic_events
      .filter(event => event.bar_offset * 4 + event.beat_offset <= localBeat + 0.001)
      .at(-1);
    const chord = chords.get(event?.chord_id);
    if (chord && !chord.pitch_classes.includes(note.pitch % 12))
      outside.push({ beat: note.startBeat, pitch: note.pitch, chord: chord.id,
        section: section.name });
  }
  const byChord = Object.entries(Object.groupBy(outside, note => note.chord))
    .map(([chord, notes]) => ({ chord, notes: notes.length }))
    .sort((a, b) => b.notes - a.notes);
  return { track: index, notes: track.notes.length, outsideDeclaredChord: outside.length,
    byChord, examples: outside.slice(0, 8) };
});
const intervalPairs = [];
for (let leftIndex = 1; leftIndex < midi.tracks.length; leftIndex++) {
  for (let rightIndex = leftIndex + 1; rightIndex < midi.tracks.length; rightIndex++) {
    for (const left of midi.tracks[leftIndex].notes) {
      for (const right of midi.tracks[rightIndex].notes) {
        const overlap = Math.min(left.startBeat + left.durationBeats,
          right.startBeat + right.durationBeats) - Math.max(left.startBeat, right.startBeat);
        if (overlap < 0.25) continue;
        const interval = Math.abs(left.pitch - right.pitch) % 12;
        if (![1, 6, 11].includes(interval)) continue;
        intervalPairs.push({ pair: `${leftIndex}-${rightIndex}`, beat: Math.max(left.startBeat,
          right.startBeat), overlap, leftPitch: left.pitch, rightPitch: right.pitch });
      }
    }
  }
}
const pairCounts = Object.entries(Object.groupBy(intervalPairs, event => event.pair))
  .map(([pair, events]) => ({ pair, events: events.length,
    overlapBeats: Math.round(events.reduce((sum, event) => sum + event.overlap, 0)),
    examples: events.slice(0, 5) }))
  .sort((a, b) => b.events - a.events);
console.log(JSON.stringify({ job: id, checkpoint: latest, bars: startBar,
  key: plan.key, tracks: result, harshIntervalPairs: pairCounts }, null, 2));
