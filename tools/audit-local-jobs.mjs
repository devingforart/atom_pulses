// Read-only local corpus audit. Never starts a worker or sends an API request.
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseMidi } from '../web/shared/midi.mjs';
import { jobDirectory, listJobIds } from '../web/local-studio/storage.mjs';

const readJson = file => {
  try { return JSON.parse(readFileSync(file, 'utf8')); } catch { return null; }
};

export function lowRegisterPairs(tracks, parts) {
  const byId = new Map((parts || []).map(part => [Number(part.part_id), part]));
  const result = [];
  for (let bassId = 1; bassId < tracks.length; bassId++) {
    if (!['movement_bass', 'sub_bass'].includes(byId.get(bassId)?.source_voice)) continue;
    for (let padId = 1; padId < tracks.length; padId++) {
      if (padId === bassId || !['harmonic_foundation', 'harmonic_pulse', 'harmonic_upper'].includes(byId.get(padId)?.source_voice)) continue;
      for (const bass of tracks[bassId].notes) for (const pad of tracks[padId].notes) {
        const distance = Math.abs(bass.pitch - pad.pitch);
        const interval = Math.min(distance % 12, 12 - distance % 12);
        const overlap = Math.min(bass.startBeat + bass.durationBeats, pad.startBeat + pad.durationBeats) - Math.max(bass.startBeat, pad.startBeat);
        if (Math.min(bass.pitch, pad.pitch) >= 55 || distance > 19 || ![1, 6].includes(interval) || overlap < 1 / 16 - 1e-6) continue;
        result.push({ beat: Math.max(bass.startBeat, pad.startBeat), bassPitch: bass.pitch, padPitch: pad.pitch, overlapBeats: +overlap.toFixed(3), bassPartId: bassId, padPartId: padId });
      }
    }
  }
  return result;
}

export function inspectJob(directory) {
  const job = readJson(path.join(directory, 'job.json'));
  if (!job) return null;
  const manifest = readJson(path.join(directory, 'manifest.json'));
  const diagnostic = readJson(path.join(directory, 'diagnostic.json'));
  const error = readJson(path.join(directory, 'error.json'));
  const midiFile = manifest?.fullFile || diagnostic?.file;
  const result = {
    id: path.basename(directory), createdAt: job.created_at,
    state: manifest ? 'completed' : error ? 'failed' : 'unfinished',
    error: error?.error || null, midiFile: midiFile || null,
    authoredNotes: null, technicalIntegrity: null, musicalReview: null,
    lowRegisterPairs: [], harmonicContext: 'unavailable_in_legacy_job',
  };
  if (!midiFile || !existsSync(path.join(directory, midiFile))) return result;
  const score = parseMidi(readFileSync(path.join(directory, midiFile)));
  const notes = score.tracks.flatMap(track => track.notes);
  result.authoredNotes = notes.length;
  result.technicalIntegrity = {
    unterminatedNotes: notes.filter(note => note.unterminated).length,
    invalidDurations: notes.filter(note => note.durationBeats <= 0 || note.startBeat < 0).length,
    offGridAttacks: notes.filter(note => Math.abs(note.startBeat * 4 - Math.round(note.startBeat * 4)) > 1e-6).length,
  };
  const stem = midiFile.replace(/\.mid$/i, '');
  const sidecar = readJson(path.join(directory, `${stem}.pulso.json`));
  result.lowRegisterPairs = lowRegisterPairs(score.tracks, sidecar?.parts);
  const audit = readJson(path.join(directory, manifest?.auditFile || diagnostic?.auditFile || 'coherence-audit.json'));
  const plan = readJson(path.join(directory, manifest?.planFile || diagnostic?.planFile || 'composition-plan.json'));
  if (audit && plan?.chord_palette) {
    result.harmonicContext = 'complete';
    result.musicalReview = {
      required: Boolean(audit.musical_review_required),
      contextualRisk: Number(audit.contextual_risk) || 0,
      aiRevisionAttempted: Boolean(manifest?.editorial?.review_attempted),
      aiRevisionAccepted: Boolean(manifest?.editorial?.review_accepted),
      aiRevisionWindows: Number(manifest?.editorial?.review_windows) || 0,
      observations: audit.musical_observations,
      contextualIssues: audit.issues,
      chordCount: plan.chord_palette.length,
    };
  }
  return result;
}

function main() {
  const args = process.argv.slice(2);
  const selected = args.find(value => /^[0-9a-f]{8}-[0-9a-f-]{27,}$/i.test(value));
  const directories = listJobIds()
    .filter(id => !selected || id === selected)
    .map(id => jobDirectory(id));
  if (!directories.length && !selected) throw new Error('No existe historial local en el proyecto ni en la ubicación anterior.');
  const reports = directories.map(inspectJob).filter(Boolean)
    .sort((a, b) => (b.createdAt || '').localeCompare(a.createdAt || ''))
    .slice(0, selected ? 1 : 20);
  process.stdout.write(JSON.stringify({ localOnly: true, apiCalls: 0, jobs: reports }, null, 2) + '\n');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
