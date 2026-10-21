// Read-only comparison of the exact first and selected AI foreground checkpoints.
import fs from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../web/shared/midi.mjs';

const id = process.argv[2];
if (!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(id || ''))
  throw new Error('Usage: node tools/compare-foreground-drafts.mjs <local-job-uuid>');
const directory = path.resolve('build/local-studio-data/jobs', id);
const readMidi = file => parseMidi(fs.readFileSync(path.join(directory, file)));
const before = readMidi('partial-01.mid'), after = readMidi('partial-02.mid');
const key = n => [n.startBeat, n.durationBeats, n.pitch, n.velocity].join('/');
const inventory = notes => {
  const counts = new Map();
  for (const n of notes) counts.set(key(n), (counts.get(key(n)) || 0) + 1);
  return counts;
};
const difference = (notes, other) => {
  const remaining = inventory(other);
  return notes.filter(note => {
    const count = remaining.get(key(note)) || 0;
    if (count) { remaining.set(key(note), count - 1); return false; }
    return true;
  });
};
const names = [...new Set([...before.tracks, ...after.tracks].filter(t => t.notes.length).map(t => t.name))];
const report = { advisoryOnly: true, qualityConclusion: null, jobId: id,
  method: 'Exact multiset comparison by named part/onset/sounding duration/pitch/velocity; no quality score or automatic note edits.',
  parts: names.map(name => {
    const a = before.tracks.find(t => t.name === name)?.notes || [];
    const b = after.tracks.find(t => t.name === name)?.notes || [];
    const removed = difference(a, b), added = difference(b, a);
    return { name, originalNotes: a.length, selectedNotes: b.length,
      unchangedNotes: a.length - removed.length, removed, added };
  }) };
fs.writeFileSync(path.join(directory, 'foreground-revision-diff.json'), JSON.stringify(report, null, 2) + '\n');
console.log(JSON.stringify({jobId:id,parts:report.parts.map(p=>({name:p.name,
  original:p.originalNotes,selected:p.selectedNotes,unchanged:p.unchangedNotes,
  removed:p.removed.length,added:p.added.length}))}));
