// Read-only reference-to-realization evidence; not an artistic grade or MIDI edit.
import fs from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../web/shared/midi.mjs';
const id = process.argv[2];
if (!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(id || '')) throw Error('Provide job UUID');
const dir = path.resolve('build/local-studio-data/jobs', id);
const read = name => JSON.parse(fs.readFileSync(path.join(dir, name), 'utf8'));
const blueprint = read('accepted-blueprint.json');
const groundwork = blueprint.composition_groundwork;
if (!groundwork) throw Error('No jointly composed groundwork in accepted blueprint');
const midi = parseMidi(fs.readFileSync(path.join(dir, 'full-song.mid')));
const phrase = read('phrase-architecture.json');
const expectedDecisions = blueprint.sections?.filter(s => s.development_strategy).length
  ? blueprint.sections.map((section, section_index) => ({ section_index,
    section_name: section.name, decision: section.development_strategy })) : null;
const tracks = midi.tracks.filter(t => t.notes.length);
const same = (a, b) => Math.abs(a - b) < 1e-7;
const results = groundwork.voices.map(voice => {
  const reference = voice.notes;
  const matches = [];
  for (const track of tracks) {
    for (let i = 0; i + reference.length <= track.notes.length; ++i) {
      const actual = track.notes.slice(i, i + reference.length);
      const offset = actual[0].startBeat - reference[0].beat;
      if (actual.every((note, j) => note.pitch === reference[j].pitch &&
          same(note.startBeat, reference[j].beat + offset) && same(note.durationBeats, reference[j].duration)))
        matches.push({ track: track.name, beat: offset, seconds: midi.beatToSeconds(offset) });
    }
  }
  return { role: voice.role, referenceNotes: reference.length, exactStatements: matches };
});
console.log(JSON.stringify({ job: id, advisoryOnly: true, musicalQualityEstablished: false,
  durationSeconds: midi.beatToSeconds(midi.lengthBeats), tracks: tracks.length,
  notes: tracks.reduce((n, t) => n + t.notes.length, 0),
  referenceLengthBeats: groundwork.length_beats,
  preservedThroughPhrasePlanning: JSON.stringify(phrase.composition_groundwork) === JSON.stringify(groundwork),
  developmentDecisions: expectedDecisions?.length || 0,
  developmentDecisionsTransferredExactly: expectedDecisions
    ? JSON.stringify(expectedDecisions) === JSON.stringify(phrase.development_strategies) : null,
  references: results,
  limitation: 'Exact statements measure identity retention, not meaningful development. Missing exact matches may be legitimate variation. Listen to continuity, hierarchy and destinations.'
}, null, 2));
