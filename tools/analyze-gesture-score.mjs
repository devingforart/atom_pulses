// Read-only losslessness check. No musical ranking and no MIDI edits.
import fs from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../web/shared/midi.mjs';
const id = process.argv[2];
if (!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(id || '')) throw Error('Provide job UUID');
const dir = path.resolve('build/local-studio-data/jobs', id);
const read = file => JSON.parse(fs.readFileSync(path.join(dir, file), 'utf8'));
const plan = read('partial-01-plan.json'), gestures = read('gesture-score.json');
const midi = parseMidi(fs.readFileSync(path.join(dir, 'partial-01.mid')));
const sort = values => values.sort((a, b) => a[0] - b[0] || a[2] - b[2] || a[1] - b[1] || a[3] - b[3]);
const owners = Object.entries(gestures.parts).map(([id, phrases]) => {
  const part = plan.cast.find(p => p.id === id);
  if (!part) throw Error(`Unknown gesture owner ${id}`);
  const authored = sort(phrases.flatMap(g => g.notes.map(n => [g.start_beat + n.beat, n.duration, n.pitch, n.velocity])));
  const realized = sort(midi.tracks.filter(t => t.name === part.name || t.name === `PULSO ${part.name}`)
    .flatMap(t => t.notes.map(n => [n.startBeat, n.durationBeats, n.pitch, n.velocity])));
  return { id, name: part.name, gestures: phrases.length, authored: authored.length, realized: realized.length,
    unchangedByTranslation: JSON.stringify(authored) === JSON.stringify(realized),
    gestureStartBeats: phrases.map(g => g.start_beat) };
});
console.log(JSON.stringify({ job: id, advisoryOnly: true, musicalQualityEstablished: false, owners,
  allAuthoredNotesPreserved: owners.every(p => p.unchangedByTranslation),
  limitation: 'Lossless gesture translation does not establish phrasing, emotional depth or narrative quality.' }, null, 2));
