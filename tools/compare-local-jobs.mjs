import { readFileSync } from 'node:fs';
import path from 'node:path';
import { jobDirectory } from '../web/local-studio/storage.mjs';

const [beforeId, afterId] = process.argv.slice(2);
const uuid = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
if (!uuid.test(beforeId || '') || !uuid.test(afterId || '')) {
  console.error('Uso: node tools/compare-local-jobs.mjs <job-anterior> <job-nuevo>');
  process.exitCode = 2;
} else {
  const read = (id, filename) => {
    try { return JSON.parse(readFileSync(path.join(jobDirectory(id), filename), 'utf8')); }
    catch { return null; }
  };
  const jobs = [beforeId, afterId].map(id => {
    const request = read(id, 'job.json');
    const manifest = read(id, 'manifest.json');
    const score = read(id, 'full-song.pulso.json');
    const plan = read(id, 'composition-plan.json');
    if (!request) throw new Error(`No existe el job ${id}`);
    return { id, request, manifest, score, plan };
  });
  const sameRequest = ['prompt', 'duration_seconds', 'bpm', 'behavior', 'seed']
    .every(key => jobs[0].request[key] === jobs[1].request[key]);
  console.log(sameRequest ? 'A/B comparable: mismo prompt, duración, BPM, enfoque y semilla.' :
    'ATENCIÓN: las solicitudes difieren; no atribuyas todas las diferencias al motor.');
  const metric = (job, key) => job.score?.[key] ?? job.manifest?.editorial?.[key] ?? '—';
  const rows = [
    ['ID', job => job.id],
    ['Título', job => job.manifest?.title || 'sin MIDI'],
    ['Barras', job => job.manifest?.bars ?? '—'],
    ['Pistas MIDI', job => job.manifest?.tracks?.length ?? '—'],
    ['Notas MIDI', job => job.manifest?.tracks?.reduce((sum, track) => sum + (Number(track.notes) || 0), 0) ?? '—'],
    ['Creatividad auditada', job => metric(job, 'creative_ready')],
    ['Narrativa auditada', job => metric(job, 'narrative_spine_ready')],
    ['Resolución', job => metric(job, 'narrative_resolution_score')],
    ['Capas viables', job => metric(job, 'retained_viability_tracks')],
    ['Capas planificadas', job => metric(job, 'arrangement_target_parts')],
    ['Compases subocupados', job => metric(job, 'underfilled_bars_after')],
    ['Diálogo musical', job => metric(job, 'dialogue_musical_lines')],
    ['Autoría IA', job => metric(job, 'ai_authored_note_ratio')],
    ['Plan auditable', job => Boolean(job.plan)],
  ];
  console.table(Object.fromEntries(rows.map(([name, value]) =>
    [name, { anterior: value(jobs[0]), nuevo: value(jobs[1]) }])));
  console.log('Estos indicadores son diagnósticos, no una puntuación estética. Escuchá los MIDI con los mismos sonidos y nivel.');
}
