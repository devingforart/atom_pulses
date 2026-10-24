import { existsSync, mkdirSync, readFileSync, readdirSync, renameSync, writeFileSync } from 'node:fs';
import path from 'node:path';
import { randomUUID } from 'node:crypto';

export const criteria = Object.freeze({
  idea: 'Idea memorable',
  connection: 'Conexión entre partes',
  melody: 'Melodía y diálogo',
  harmony: 'Armonía',
  rhythm: 'Ritmo y fraseo',
  evolution: 'Evolución y contraste',
  climax: 'Clímax',
  ending: 'Cierre',
  usefulness: 'Utilidad para producir',
});
export const problemTags = Object.freeze([
  'sin_idea', 'repeticion_vacia', 'voces_desconectadas', 'disonancia',
  'ritmo_torpe', 'demasiadas_notas', 'muy_vacio', 'climax_plano',
  'final_abrupto', 'timbre_confunde', 'otro',
]);
const idPattern = /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i;
const read = file => { try { return JSON.parse(readFileSync(file, 'utf8')); } catch { return null; } };
const assertId = id => { if (!idPattern.test(id)) throw new Error('Identificador inválido.'); };
const atomicJson = (file, value) => {
  mkdirSync(path.dirname(file), { recursive: true });
  const temp = `${file}.${randomUUID()}.tmp`;
  writeFileSync(temp, JSON.stringify(value, null, 2), { flag: 'wx' });
  renameSync(temp, file);
};
const plain = value => value && typeof value === 'object' && !Array.isArray(value);
const text = (value, limit) => typeof value === 'string' && value.trim().length <= limit ? value.trim() : null;

export function validateEvaluation(input) {
  if (!plain(input) || !plain(input.scores)) throw new Error('Completá la ficha de evaluación.');
  const scores = {};
  for (const key of Object.keys(criteria)) {
    const value = input.scores[key];
    if (!Number.isInteger(value) || value < 1 || value > 5)
      throw new Error(`Puntuá ${criteria[key]} del 1 al 5.`);
    scores[key] = value;
  }
  if (!['full', 'excerpt'].includes(input.scope) ||
      !['piano', 'neutral', 'live', 'other'].includes(input.listening_mode) ||
      !['yes', 'maybe', 'no'].includes(input.would_use) ||
      !['low', 'medium', 'high'].includes(input.confidence))
    throw new Error('Indicá cómo escuchaste y si usarías el MIDI.');
  if (!Array.isArray(input.problems) || input.problems.length > 6 ||
      new Set(input.problems).size !== input.problems.length ||
      input.problems.some(tag => !problemTags.includes(tag)))
    throw new Error('Seleccioná hasta seis problemas reconocidos.');
  const observation = text(input.observation, 1200);
  const bestMoment = text(input.best_moment, 240);
  const worstMoment = text(input.worst_moment, 240);
  if (observation === null || bestMoment === null || worstMoment === null)
    throw new Error('La observación o el compás indicado es demasiado largo.');
  return { schema_version: 1, scores, scope: input.scope,
    listening_mode: input.listening_mode, would_use: input.would_use,
    confidence: input.confidence, problems: input.problems,
    observation, best_moment: bestMoment, worst_moment: worstMoment };
}

export function evaluationStore(root) {
  const directory = path.join(root, 'evaluations');
  const file = id => { assertId(id); return path.join(directory, `${id}.json`); };
  return {
    get: id => read(file(id)),
    save: (id, input) => {
      const value = { ...validateEvaluation(input), job_id: id,
        updated_at: new Date().toISOString() };
      atomicJson(file(id), value);
      return value;
    },
    all: () => existsSync(directory) ? readdirSync(directory)
      .filter(name => idPattern.test(name.replace(/\.json$/, '')) && name.endsWith('.json'))
      .map(name => read(path.join(directory, name))).filter(Boolean) : [],
  };
}

export function previousComparableEvaluation(currentJob, savedEvaluations, lookupJob) {
  if (!currentJob?.request) return null;
  const keys = ['prompt', 'duration_seconds', 'bpm', 'behavior', 'seed'];
  const currentCreated = Date.parse(currentJob.request.created_at || '');
  const candidates = savedEvaluations.flatMap(evaluation => {
    if (evaluation.job_id === currentJob.id) return [];
    const job = lookupJob(evaluation.job_id);
    if (!job?.request || !keys.every(key => job.request[key] === currentJob.request[key])) return [];
    const created = Date.parse(job.request.created_at || '');
    if (!Number.isFinite(created) || (Number.isFinite(currentCreated) && created > currentCreated)) return [];
    return [{ job_id: job.id, title: job.manifest?.title || 'Obra anterior',
      created_at: job.request.created_at, evaluation }];
  });
  candidates.sort((a, b) => Date.parse(b.created_at) - Date.parse(a.created_at) ||
    Date.parse(b.evaluation.updated_at || '') - Date.parse(a.evaluation.updated_at || ''));
  return candidates[0] || null;
}

export function comparisonStore(root) {
  const directory = path.join(root, 'comparisons');
  const file = id => { assertId(id); return path.join(directory, `${id}.json`); };
  return {
    get: id => read(file(id)),
    create: (firstId, secondId) => {
      assertId(firstId); assertId(secondId);
      if (firstId === secondId) throw new Error('Elegí dos obras distintas.');
      const id = randomUUID();
      const sides = Math.random() < 0.5 ? { A: firstId, B: secondId } :
        { A: secondId, B: firstId };
      const value = { schema_version: 1, id, sides,
        created_at: new Date().toISOString(), vote: null };
      atomicJson(file(id), value);
      return value;
    },
    vote: (id, input) => {
      const current = read(file(id));
      if (!current) throw new Error('Comparación no encontrada.');
      if (current.vote) throw new Error('Esta comparación ya fue votada.');
      if (!plain(input) || !['A', 'B', 'tie', 'neither'].includes(input.winner) ||
          !['full', 'excerpt'].includes(input.scope) ||
          !['piano', 'neutral'].includes(input.listening_mode) ||
          typeof input.reason !== 'string' || input.reason.length > 800)
        throw new Error('Indicá tu preferencia y el motivo (máximo 800 caracteres).');
      const value = { ...current, vote: { winner: input.winner,
        scope: input.scope, listening_mode: input.listening_mode,
        reason: input.reason.trim(), at: new Date().toISOString() } };
      atomicJson(file(id), value);
      return value;
    },
    all: () => existsSync(directory) ? readdirSync(directory)
      .filter(name => idPattern.test(name.replace(/\.json$/, '')) && name.endsWith('.json'))
      .map(name => read(path.join(directory, name))).filter(Boolean) : [],
  };
}

export function publicComparison(value) {
  return value.vote ? value : { id: value.id, created_at: value.created_at,
    voted: false, sides: ['A', 'B'] };
}

export function evaluationReport(evaluations, comparisons, lookupJob) {
  const rows = evaluations.map(evaluation => {
    const job = lookupJob(evaluation.job_id);
    if (!job) return null;
    return { job_id: evaluation.job_id, title: job.manifest?.title || '',
      prompt: job.request.prompt, behavior: job.request.behavior,
      benchmark_case_id: job.request.benchmark_case_id || null,
      first_pass_audition: job.request.first_pass_audition ?? null,
      duration_seconds: job.request.duration_seconds, seed: job.request.seed,
      created_at: job.request.created_at, evaluation,
      engine: job.engine || null, model: job.telemetry?.model || null,
      audit: { technical_ready: job.manifest?.editorial?.technical_ready ?? null,
        narrative_ready: job.manifest?.editorial?.narrative_ready ?? null,
        soundscape_ready: job.manifest?.editorial?.soundscape_ready ?? null,
        harmonic_conflicts: job.manifest?.editorial?.harmonic_conflicts ?? null,
        low_register_conflicts: job.manifest?.editorial?.low_register_conflicts ?? null,
        exact_repetition_ratio: job.compositionAudit?.exactRepeatedEightBarRatio ?? null },
      estimated_usd: job.telemetry?.estimatedUsd ?? null };
  }).filter(Boolean);
  const averages = Object.fromEntries(Object.keys(criteria).map(key => [key,
    rows.length ? Math.round(rows.reduce((sum, row) =>
      sum + row.evaluation.scores[key], 0) / rows.length * 100) / 100 : null]));
  const problemCounts = Object.fromEntries(problemTags.map(tag => [tag,
    rows.filter(row => row.evaluation.problems.includes(tag)).length]));
  const votes = comparisons.filter(item => item.vote).map(item => ({
    comparison_id: item.id, first_job_id: item.sides.A,
    second_job_id: item.sides.B, ...item.vote }));
  const summarize = group => ({ count: group.length,
    would_use_yes: group.filter(row => row.evaluation.would_use === 'yes').length,
    averages: Object.fromEntries(Object.keys(criteria).map(key => [key,
      Math.round(group.reduce((sum, row) => sum + row.evaluation.scores[key], 0) /
        group.length * 100) / 100])) });
  const groupBy = selector => {
    const groups = new Map();
    for (const row of rows) {
      const key = selector(row);
      groups.set(key, [...(groups.get(key) || []), row]);
    }
    return Object.fromEntries([...groups].map(([key, group]) => [key, summarize(group)]));
  };
  return { schema_version: 1, criteria, evaluated_works: rows.length,
    would_use: { yes: rows.filter(row => row.evaluation.would_use === 'yes').length,
      maybe: rows.filter(row => row.evaluation.would_use === 'maybe').length,
      no: rows.filter(row => row.evaluation.would_use === 'no').length },
    criterion_averages: averages, problem_counts: problemCounts,
    by_benchmark: groupBy(row => row.benchmark_case_id || 'free'),
    by_engine: groupBy(row => row.engine?.workerSha256?.slice(0, 12) || 'unknown'),
    by_editorial_mode: groupBy(row => row.first_pass_audition === true ? 'first_pass' :
      row.first_pass_audition === false ? 'review_enabled' : 'unknown'),
    blind_comparisons: votes, works: rows };
}
