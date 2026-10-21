// Read-only symbolic evidence. Matching a theme does not establish musical quality.
import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseMidi } from '../web/shared/midi.mjs';

export function subjectReturns(subject, actual) {
  const source = [...subject].sort((a, b) => a.beat - b.beat || a.pitch - b.pitch);
  const notes = [...actual].sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch);
  if (!source.length) return [];
  const results = [];
  for (let start = 0; start < notes.length; start++) {
    let best = null;
    for (const scale of [1, 2, 0.5]) {
      const transpose = notes[start].pitch - source[0].pitch;
      let count = 0;
      while (count < source.length && start + count < notes.length) {
        const a = notes[start + count], b = source[count];
        if (a.pitch !== b.pitch + transpose ||
            Math.abs(a.startBeat - notes[start].startBeat - (b.beat - source[0].beat) * scale) > 1e-4 ||
            Math.abs(a.durationBeats - b.duration * scale) > 1e-4) break;
        count++;
      }
      if (!best || count > best.matchedNotes)
        best = { startBeat: notes[start].startBeat, transpose, timeScale: scale,
          matchedNotes: count, referenceNotes: source.length,
          completeReference: count === source.length };
    }
    if (best.matchedNotes >= Math.min(3, source.length)) results.push(best);
  }
  return results;
}

export function selectedForegroundFile(rehearsal) {
  return rehearsal?.status?.startsWith('AI rehearsal revision accepted;')
    ? 'partial-02.mid' : 'partial-01.mid';
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) {
  const id = process.argv[2];
  if (!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(id || ''))
    throw new Error('Usage: node tools/analyze-thematic-subject.mjs <local-job-uuid>');
  const directory = path.resolve('build/local-studio-data/jobs', id);
  const read = name => JSON.parse(fs.readFileSync(path.join(directory, name), 'utf8'));
  const architecture = read('phrase-architecture.json');
  const subject = architecture.musical_subject;
  if (!subject?.notes?.length) throw new Error('This work has no AI-authored reference subject');
  const plan = read('composition-plan.json');
  const midi = parseMidi(fs.readFileSync(path.join(directory, 'full-song.mid')));
  const rehearsalPath = path.join(directory, 'thematic-rehearsal.json');
  const rehearsal = fs.existsSync(rehearsalPath) ? read('thematic-rehearsal.json') : null;
  const foregroundFile = selectedForegroundFile(rehearsal);
  const foreground = parseMidi(fs.readFileSync(path.join(directory, foregroundFile)));
  const key = note => [note.startBeat, note.durationBeats, note.pitch, note.velocity].join('/');
  const foregroundPreservation = foreground.tracks.filter(track => track.notes.length).map(track => {
    const final = midi.tracks.find(other => other.name === track.name);
    return { name: track.name, notes: track.notes.length,
      exactlyPreserved: JSON.stringify(track.notes.map(key)) === JSON.stringify(final?.notes.map(key)) };
  });
  const report = { advisoryOnly: true, qualityConclusion: null,
    method: 'Contiguous reference-prefix pitch, onset and sounding-duration matches; transposition and rhythmic scaling are evidence only, never applied to MIDI. Modified motives and non-prefix transformations may not match.',
    jobId: id, title: plan.title, reference: subject,
    selectedForegroundFile: foregroundFile, originalForegroundFile: 'partial-01.mid',
    rehearsalStatus: rehearsal?.status || null,
    foregroundPreservation,
    tracks: midi.tracks.filter(track => track.notes.length).map(track => ({
      name: track.name, notes: track.notes.length,
      matches: subjectReturns(subject.notes, track.notes).map(match => ({ ...match,
        seconds: midi.beatToSeconds(match.startBeat),
        barOneBased: Math.floor(match.startBeat / midi.beatsPerBar) + 1 })),
    })) };
  fs.writeFileSync(path.join(directory, 'thematic-subject-audit.json'), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({ title: report.title, referenceNotes: subject.notes.length,
    foregroundPreservation, matches: report.tracks.filter(track => track.matches.length) }));
}
