// Immutable score correspondence, not a musical score or repair algorithm.
import fs from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../web/shared/midi.mjs';
const id = process.argv[2];
if (!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(id || '')) throw Error('Provide job UUID');
const dir = path.resolve('build/local-studio-data/jobs', id);
const read = file => JSON.parse(fs.readFileSync(path.join(dir, file), 'utf8'));
const plan = read('composition-plan.json'), phrase = read('phrase-architecture.json');
const midi = file => parseMidi(fs.readFileSync(path.join(dir, file)));
const initial = midi('partial-01.mid'), selected = midi('partial-02.mid'), full = midi('full-song.mid');
const signature = (score, name) => score.tracks.filter(t => t.name === name || t.name === `PULSO ${name}`)
  .flatMap(t => t.notes).map(n => [n.startBeat, n.durationBeats, n.pitch, n.velocity])
  .sort((a, b) => a[0] - b[0] || a[2] - b[2] || a[1] - b[1]);
const owners = phrase.argument_owner_ids.map(id => {
  const part = plan.cast.find(p => p.id === id);
  if (!part) throw Error(`Unknown argument owner ${id}`);
  const before = signature(initial, part.name), chosen = signature(selected, part.name), after = signature(full, part.name);
  return { id, name: part.name, firstDraftNotes: before.length, selectedNotes: chosen.length,
    finalNotes: after.length, unchangedByOrchestration: JSON.stringify(chosen) === JSON.stringify(after) };
});
const trace = fs.readFileSync(path.join(dir, 'api-events.jsonl'), 'utf8').trim().split('\n').map(JSON.parse);
const rehearsal = read('thematic-rehearsal.json');
console.log(JSON.stringify({ job: id, advisoryOnly: true, musicalQualityEstablished: false,
  title: plan.title, durationSeconds: full.beatToSeconds(full.lengthBeats), owners,
  wholeArgumentCallsIncludingTechnicalRetries: trace.filter(c => c.phase === 'pulso_complete_dialogue').length,
  reviewStatus: rehearsal.status, reviewDiagnosis: rehearsal.response?.diagnosis,
  allArgumentNotesPreservedByOrchestration: owners.every(o => o.unchangedByOrchestration),
  directAuthorship: read('manifest.json').note_provenance,
  limitation: 'Exact note preservation establishes pipeline integrity, not a compelling musical narrative.'
}, null, 2));
