import http from 'node:http';
import { spawn } from 'node:child_process';
import { createHash, randomBytes, randomUUID } from 'node:crypto';
import { existsSync, createReadStream, mkdirSync, readFileSync, statSync, writeFileSync, appendFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { dataRoot, jobsRoot, jobDirectory, listJobIds } from './storage.mjs';
import { analyzeComposition } from './composition-audit.mjs';
import { analyzePhraseArchitectureFiles } from './phrase-audit.mjs';
import { canAutomaticallyRecover, automaticRecoveryRequest,
  AUTOMATIC_RECOVERY_MAX_SECONDS, AUTOMATIC_RECOVERY_MAX_USD } from './recovery-policy.mjs';
import { parseMidi } from '../shared/midi.mjs';
import { composerProfile, defaultEditorialSettings, experimentalProfile, workerEnvironmentFor } from './composer-environment.mjs';
import { originForJob, originForRoute } from './job-origin.mjs';
import { blueprintRequestMatchesSource, exactResumeMatchesCheckpoint } from './checkpoint-integrity.mjs';
import { foregroundCheckpoint } from './foreground-checkpoint.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
const shared = path.resolve(here, '..', 'shared');
const worker = process.env.PULSO_LOCAL_WORKER_PATH || path.join(root, 'build-cloud', 'Release', 'pulso_cloud_worker.exe');
const port = Number(process.env.PULSO_LOCAL_PORT || 4177);
if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Invalid PULSO_LOCAL_PORT');
mkdirSync(jobsRoot, { recursive: true });

let active = null;
const stages = { blueprint: 'Diseñando la obra', writing: 'Escribiendo las pistas', recovery: 'Revisando material', validation: 'Validando la composición', rendering: 'Preparando MIDI', ready: 'Lista' };

function json(res, status, body) {
  res.writeHead(status, { 'Content-Type': 'application/json; charset=utf-8', 'Cache-Control': 'no-store' });
  res.end(JSON.stringify(body));
}

function readJson(file) {
  try { return JSON.parse(readFileSync(file, 'utf8')); } catch { return null; }
}

function explainJobFailure(message) {
  if (!message) return null;
  if (/no credits remaining|insufficient_quota/i.test(message))
    return 'OpenAI rechazó la solicitud porque este proyecto no tiene créditos disponibles. Revisá la facturación o cargá créditos antes de volver a componer; no se generó ningún MIDI.';
  return message;
}

function readJsonLines(file) {
  try { return readFileSync(file, 'utf8').trim().split(/\r?\n(?=\{)/).filter(Boolean).slice(-2000).flatMap(record => { try { return [JSON.parse(record)]; } catch { return []; } }); }
  catch { return []; }
}

function estimatedUsd(call) {
  if (!call.usage_available || !['gpt-5.6-terra', 'gpt-6.1-sol', 'gpt-6-luna'].includes(call.model)) return null;
  const input = Number(call.input_tokens) || 0;
  const cached = Number(call.cached_input_tokens) || 0;
  const writes = Number(call.cache_write_tokens) || 0;
  const output = Number(call.output_tokens) || 0;
  const ordinary = Math.max(0, input - cached - writes);
  const longContext = input > 272000;
  const fast = ['priority', 'fast'].includes(call.service_tier_actual);
  const rates = call.model === 'gpt-6-luna' ? [0.1, 0.01, 0.125, 0.5] :
    call.model === 'gpt-6.1-sol' ? [2, 0.1, 2.5, 10] : [2, 0.2, 2.5, 12];
  if (fast) for (let i = 0; i < rates.length; i++) rates[i] *= 2;
  return ((ordinary * rates[0] + cached * rates[1] + writes * rates[2]) *
    (longContext ? 2 : 1) + output * rates[3] * (longContext ? 1.5 : 1)) / 1000000;
}

function traceFor(id, createdAt, endedAt = Date.now()) {
  const dir = jobDirectory(id);
  const apiCalls = readJsonLines(path.join(dir, 'api-events.jsonl')).map(call => ({ ...call, estimatedUsd: estimatedUsd(call) }));
  const progressEvents = readJsonLines(path.join(dir, 'progress-events.jsonl'));
  const totals = { calls: apiCalls.length, callsWithUsage: 0, localReuseCalls: 0, pricedCalls: 0, estimatedUsd: 0, priceAsOf: '2026-10-12', priceSource: 'https://developers.openai.com/api/docs/pricing', inputTokens: 0, cachedInputTokens: 0, cacheWriteTokens: 0, outputTokens: 0, reasoningTokens: 0, totalTokens: 0, timedOutCalls: 0, incompleteCalls: 0, failedCalls: 0, recoveryEvents: progressEvents.filter(event => event.stage === 'recovery').length, summedApiMs: 0, model: apiCalls.find(call => call.model)?.model || null, effort: apiCalls.find(call => call.effort)?.effort || null };
  for (const call of apiCalls) {
    totals.summedApiMs += Number(call.elapsed_ms) || 0;
    if (call.reused_locally) totals.localReuseCalls++;
    if (call.estimatedUsd !== null) { totals.estimatedUsd += call.estimatedUsd; totals.pricedCalls++; }
    if (call.timed_out) totals.timedOutCalls++;
    if (call.remote_status === 'incomplete') totals.incompleteCalls++;
    if (!call.connected || call.http_status < 200 || call.http_status >= 300 || call.timed_out || call.cancelled || ['failed', 'incomplete', 'cancelled'].includes(call.remote_status)) totals.failedCalls++;
    if (!call.usage_available) continue;
    totals.callsWithUsage++;
    for (const [source, target] of [['input_tokens', 'inputTokens'], ['cached_input_tokens', 'cachedInputTokens'], ['cache_write_tokens', 'cacheWriteTokens'], ['output_tokens', 'outputTokens'], ['reasoning_tokens', 'reasoningTokens'], ['total_tokens', 'totalTokens']]) totals[target] += Number(call[source]) || 0;
  }
  const phaseMs = {};
  for (let index = 0; index < progressEvents.length; index++) {
    const current = progressEvents[index];
    const next = progressEvents[index + 1];
    const elapsed = Math.max(0, (next ? Date.parse(next.at) : endedAt) - Date.parse(current.at));
    if (current.stage && Number.isFinite(elapsed)) phaseMs[current.stage] = (phaseMs[current.stage] || 0) + elapsed;
  }
  return { summary: { ...totals, phaseMs, wallMs: Math.max(0, endedAt - Date.parse(createdAt)) }, apiCalls, progressEvents };
}

function jobDetails(id) {
  const dir = jobDirectory(id);
  if (!dir) return null;
  const request = readJson(path.join(dir, 'job.json'));
  if (!request) return null;
  const origin = originForJob(readJson(path.join(dir, 'local-origin.json')));
  const manifest = readJson(path.join(dir, 'manifest.json'));
  let compositionAudit = readJson(path.join(dir, 'composition-audit.json'));
  if (manifest && !compositionAudit) {
    try {
      compositionAudit = analyzeComposition(dir, manifest);
      writeFileSync(path.join(dir, 'composition-audit.json'), JSON.stringify(compositionAudit, null, 2));
    } catch { /* An optional audit must never hide a completed MIDI score. */ }
  }
  let phraseAudit = readJson(path.join(dir, 'phrase-audit.json'));
  if (manifest?.phraseArchitectureFile && !phraseAudit) {
    try {
      phraseAudit = analyzePhraseArchitectureFiles(dir);
      writeFileSync(path.join(dir, 'phrase-audit.json'), JSON.stringify(phraseAudit, null, 2));
    } catch { /* Optional audit must never hide a completed MIDI score. */ }
  }
  // MIDI sidecars are produced by the existing exporter. Read them server-side
  // so older jobs also get sound-role metadata without rebuilding the worker.
  if (manifest?.tracks) manifest.tracks = manifest.tracks.map(track => {
    if (!/^track-[0-9]+\.mid$/i.test(track.filename || '')) return track;
    const sidecar = readJson(path.join(dir, track.filename.replace(/\.mid$/i, '.pulso.json')));
    const part = sidecar?.parts?.[0];
    return part ? { ...track, instrument: {
      catalog_id: part.catalog_id, department: part.department,
      source_voice: part.source_voice, role: part.role,
      orchestral_function: part.orchestral_function,
      articulation: part.articulation,
      live_preset_intent: part.live_preset_intent,
    } } : track;
  });
  const checkpoint = readJson(path.join(dir, 'checkpoint.json'));
  const recovery = readJson(path.join(dir, 'recovery.json'));
  const diagnostic = readJson(path.join(dir, 'diagnostic.json'));
  const progress = readJson(path.join(dir, 'progress.json'));
  const failure = readJson(path.join(dir, 'error.json'));
  const cancelled = existsSync(path.join(dir, 'cancelled.json'));
  let state = manifest ? 'completed' : cancelled ? 'cancelled' : failure ? 'failed' : progress?.state || 'queued';
  if (state === 'running' && active?.id !== id) state = 'interrupted';
  const terminalFile = state === 'completed' ? 'manifest.json' : state === 'failed' ? 'error.json' : state === 'cancelled' ? 'cancelled.json' : null;
  let endedAt = Date.now();
  if (terminalFile) {
    try { endedAt = statSync(path.join(dir, terminalFile)).mtimeMs; } catch { /* Keep elapsed time. */ }
  }
  const trace = traceFor(id, request.created_at, endedAt);
  return { id, origin, request, state, stage: stages[progress?.stage] || progress?.stage || '', completed: progress?.completed || 0, total: progress?.total || 0, manifest, compositionAudit, phraseAudit, checkpoint, recovery, diagnostic, telemetry: trace.summary, error: state === 'cancelled' ? null : explainJobFailure(failure?.error) || (state === 'interrupted' ? 'El proceso se interrumpió. Los archivos existentes se conservaron.' : null) };
}

function listJobs() {
  const counts = { personal: 0, auto: 0 };
  return listJobIds()
    .map(id => jobDetails(id))
    .filter(Boolean)
    .sort((a, b) => b.request.created_at.localeCompare(a.request.created_at))
    .filter(job => ++counts[job.origin] <= 50);
}

async function bodyJson(req) {
  let raw = '';
  for await (const chunk of req) {
    raw += chunk;
    if (raw.length > 8192) throw new InputError('La solicitud es demasiado grande.');
  }
  try { return JSON.parse(raw); } catch { throw new InputError('La solicitud no es JSON válido.'); }
}

class InputError extends Error {}

function validate(input) {
  if (input === null || typeof input !== 'object' || Array.isArray(input)) throw new InputError('La solicitud debe ser un objeto JSON.');
  const prompt = typeof input.prompt === 'string' ? input.prompt.trim() : '';
  const duration = input.duration_seconds;
  const bpm = input.bpm;
  const behavior = input.behavior || 'adaptive';
  const renderMode = input.render_mode || 'ai_sovereign';
  const suppliedSeed = input.seed === undefined || input.seed === '' ? null : String(input.seed);
  const proofMode = input.proof_mode === true;
  const { firstPass: firstPassAudition,
    phraseArchitecture: phraseArchitectureExperiment } =
    defaultEditorialSettings(input, proofMode, renderMode);
  const resumeFromJob = input.resume_from_job === undefined ? null : String(input.resume_from_job);
  const blueprintFromJob = input.blueprint_from_job === undefined ? null : String(input.blueprint_from_job);
  const replanPhrases = input.replan_phrases === true;
  const compareEditorialModes = input.compare_editorial_modes === true;
  const reviewFromJob = input.review_from_job === undefined ? null : String(input.review_from_job);
  const foregroundFromJob = input.foreground_from_job === undefined ? null : String(input.foreground_from_job);
  if (!prompt || prompt.length > 600) throw new InputError('La idea debe contener entre 1 y 600 caracteres.');
  if (!Number.isInteger(duration) || duration < 30 || duration > 900) throw new InputError('La duración debe estar entre 30 y 900 segundos.');
  if (!Number.isFinite(bpm) || bpm < 60 || bpm > 180) throw new InputError('El tempo debe estar entre 60 y 180 BPM.');
  if (!['adaptive', 'hypnotic', 'narrative'].includes(behavior)) throw new InputError('El enfoque musical no es válido.');
  if (!['ai_sovereign', 'standard'].includes(renderMode)) throw new InputError('El modo de render no es válido.');
  if (suppliedSeed !== null && (!/^[1-9][0-9]{0,18}$/.test(suppliedSeed) || BigInt(suppliedSeed) > 9223372036854775807n)) throw new InputError('La semilla debe ser un entero positivo de 64 bits.');
  const seed = suppliedSeed || String((randomBytes(8).readBigUInt64LE() & 9223372036854775807n) || 1n);
  if (proofMode && duration > 60) throw new InputError('La prueba de coherencia solo admite obras de hasta 60 segundos.');
  if (firstPassAudition && (proofMode || renderMode !== 'ai_sovereign'))
    throw new InputError('La escucha de primera pasada requiere IA soberana y prueba de coherencia desactivada.');
  if (phraseArchitectureExperiment && (proofMode || renderMode !== 'ai_sovereign'))
    throw new InputError('El plan de frases experimental requiere IA soberana y prueba de coherencia desactivada.');
  if (proofMode && renderMode !== 'ai_sovereign') throw new InputError('La prueba de coherencia requiere el modo de autoría IA.');
  if (resumeFromJob !== null && (!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(resumeFromJob) || renderMode !== 'ai_sovereign' || proofMode))
    throw new InputError('Invalid local AI resume job.');
  if (blueprintFromJob !== null && (!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(blueprintFromJob) ||
      resumeFromJob !== null || renderMode !== 'ai_sovereign' || proofMode))
    throw new InputError('Invalid controlled A/B blueprint job.');
  if (replanPhrases && (!experimentalProfile || !blueprintFromJob || !phraseArchitectureExperiment))
    throw new InputError('Phrase replanning is available only for a controlled experimental local A/B.');
  if (compareEditorialModes && (!experimentalProfile || !blueprintFromJob))
    throw new InputError('Editorial-mode comparison requires an explicit experimental local blueprint A/B.');
  if (reviewFromJob !== null && (!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(reviewFromJob) ||
      resumeFromJob !== null || blueprintFromJob !== null || firstPassAudition || proofMode || renderMode !== 'ai_sovereign'))
    throw new InputError('Invalid first-pass review source.');
  if (foregroundFromJob !== null && (!experimentalProfile ||
      process.env.PULSO_AI_THEMATIC_REHEARSAL_EXPERIMENT !== '1' ||
      !/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(foregroundFromJob) ||
      resumeFromJob !== null || blueprintFromJob !== null || reviewFromJob !== null ||
      renderMode !== 'ai_sovereign' || proofMode || !firstPassAudition || !phraseArchitectureExperiment))
    throw new InputError('Exact foreground rehearsal is available only in its experimental local worker.');
  return { prompt, duration_seconds: duration, bpm, behavior, seed, ai_sovereign: renderMode === 'ai_sovereign', editorial_profile: renderMode === 'ai_sovereign' ? 'ai-only-v2' : 'standard', proof_mode: proofMode, first_pass_audition: firstPassAudition, phrase_architecture_experiment: phraseArchitectureExperiment, replan_phrases: replanPhrases, compare_editorial_modes: compareEditorialModes, resume_from_job: resumeFromJob, blueprint_from_job: blueprintFromJob, review_from_job: reviewFromJob, ...(foregroundFromJob ? {foreground_from_job: foregroundFromJob} : {}), created_at: new Date().toISOString() };
}

function foregroundScoreFromJob(request) {
  const sourceDir = jobDirectory(request.foreground_from_job);
  const source = sourceDir && readJson(path.join(sourceDir, 'job.json'));
  if (!blueprintRequestMatchesSource(source, request))
    throw new InputError('Exact foreground rehearsal must retain prompt, duration, tempo, seed and modes.');
  const resume = readJson(path.join(sourceDir, 'resume-score.json'));
  const plan = readJson(path.join(sourceDir, 'partial-01-plan.json'));
  if (!resume || !plan || !existsSync(path.join(sourceDir, 'partial-01.mid')))
    throw new InputError('This source has no complete first foreground checkpoint.');
  try {
    return foregroundCheckpoint(resume, plan,
      parseMidi(readFileSync(path.join(sourceDir, 'partial-01.mid'))));
  } catch (error) { throw new InputError(error.message); }
}

function blueprintOnlyScoreFromJob(request) {
  const sourceDir = jobDirectory(request.blueprint_from_job);
  const source = sourceDir && readJson(path.join(sourceDir, 'job.json'));
  const allowEditorialChange = experimentalProfile && request.compare_editorial_modes === true &&
    existsSync(path.join(sourceDir, 'manifest.json'));
  if (!blueprintRequestMatchesSource(source, request, allowEditorialChange))
    throw new InputError('Controlled A/B request must match the source prompt, duration, tempo, seed and mode.');
  const blueprintFile = path.join(sourceDir, 'accepted-blueprint.json');
  const blueprintJson = existsSync(blueprintFile) && statSync(blueprintFile).size <= 2 * 1024 * 1024
    ? readFileSync(blueprintFile, 'utf8') : null;
  const blueprint = blueprintJson && JSON.parse(blueprintJson);
  const checkpoint = readJson(path.join(sourceDir, 'checkpoint.json'));
  const checkpointPlan = typeof checkpoint?.planFile === 'string' &&
    /^partial-[0-9]+-plan\.json$/.test(checkpoint.planFile)
      ? checkpoint.planFile : null;
  const plan = readJson(path.join(sourceDir, 'composition-plan.json')) ||
    (checkpointPlan ? readJson(path.join(sourceDir, checkpointPlan)) : null);
  // A failure in the first writing window has no MIDI checkpoint yet. The
  // accepted macro/cast is still valuable and can be retried without paying
  // for the same blueprint again; a new phrase plan is authored if none was
  // persisted. No provisional notes are promoted by this route.
  const cast = Array.isArray(plan?.cast) ? plan.cast : blueprint?.instruments;
  let offset = 0;
  const sections = Array.isArray(plan?.sections) ? plan.sections :
    Array.isArray(blueprint?.sections) ? blueprint.sections.map(section => {
      const item = { start_bar: offset, bars: section.bars };
      offset += section.bars;
      return item;
    }) : null;
  if (!blueprint || !Array.isArray(cast) || !Array.isArray(sections) ||
      !cast.length || !sections.length ||
      cast.some(part => typeof part?.id !== 'string' || !part.id) ||
      sections.some(section => !Number.isInteger(section?.start_bar) ||
        !Number.isInteger(section?.bars) || section.bars < 1))
    throw new InputError('Source job has no complete accepted AI blueprint.');
  const phraseFile = path.join(sourceDir, 'phrase-architecture.json');
  const sourceCheckpointFile = path.join(sourceDir, 'resume-score.json');
  const checkpointPhrase = experimentalProfile &&
    existsSync(sourceCheckpointFile) && statSync(sourceCheckpointFile).size <= 2 * 1024 * 1024
      ? readJson(sourceCheckpointFile)?.phrase_architecture_json : null;
  const phraseArchitectureJson = request.phrase_architecture_experiment && !request.replan_phrases &&
    existsSync(phraseFile) && statSync(phraseFile).size <= 2 * 1024 * 1024
      ? readFileSync(phraseFile, 'utf8') :
        request.phrase_architecture_experiment && !request.replan_phrases &&
        typeof checkpointPhrase === 'string' ? checkpointPhrase : null;
  if (request.phrase_architecture_experiment && !request.replan_phrases &&
      !phraseArchitectureJson && (checkpoint || plan))
    throw new InputError('Source job has no accepted phrase architecture for an exact A/B.');
  return { schema_version: 2, blueprint_only: true, blueprint_json: blueprintJson,
    ...(phraseArchitectureJson ? { phrase_architecture_json: phraseArchitectureJson } : {}),
    cast_ids: cast.map(part => part.id),
    sections: sections.map(section => ({ start_bar: section.start_bar, bars: section.bars })),
    notes: [] };
}

function resumeScoreFromJob(request, controlledReview = false) {
  const previousDirectory = jobDirectory(controlledReview ? request.review_from_job : request.resume_from_job);
  if (!previousDirectory || !existsSync(previousDirectory)) throw new InputError('Previous local job was not found.');
  const previous = readJson(path.join(previousDirectory, 'job.json'));
  const same = ['prompt', 'duration_seconds', 'bpm', 'behavior', 'seed', 'ai_sovereign']
    .every(field => previous?.[field] === request[field]) &&
    Boolean(previous?.phrase_architecture_experiment) === Boolean(request.phrase_architecture_experiment) &&
    (controlledReview ? previous?.first_pass_audition === true && request.first_pass_audition === false :
      Boolean(previous?.first_pass_audition) === Boolean(request.first_pass_audition));
  if (!same) throw new InputError('Prompt, duration, tempo, seed and mode must match the accepted checkpoint.');
  if (controlledReview && (!existsSync(path.join(previousDirectory, 'manifest.json')) ||
      !existsSync(path.join(previousDirectory, 'full-song.mid')) ||
      readJson(path.join(previousDirectory, 'manifest.json'))?.first_pass_audition !== true))
    throw new InputError('Review source must be a completed first-pass composition.');
  const checkpoint = readJson(path.join(previousDirectory, 'checkpoint.json'));
  if (!checkpoint?.file || !/^partial-[0-9]+\.mid$/.test(checkpoint.file))
    throw new InputError('Previous job has no accepted MIDI checkpoint.');
  const exactResume = readJson(path.join(previousDirectory, 'resume-score.json'));
  if (exactResumeMatchesCheckpoint(checkpoint, exactResume)) {
      if (controlledReview) {
        const expectedWindows = exactResume.sections.reduce((sum, section) =>
          sum + Math.ceil(section.bars / 16), 0);
        if (exactResume.completed_units !== expectedWindows)
          throw new InputError('Review source does not contain every completed musical window.');
      }
      return exactResume;
    }
  if (controlledReview) throw new InputError('Controlled review requires the exact completed AI score.');
  const stem = checkpoint.file.slice(0, -4);
  const plan = readJson(path.join(previousDirectory, `${stem}-plan.json`));
  const score = readJson(path.join(previousDirectory, `${stem}.pulso.json`));
  if (!Array.isArray(plan?.cast) || !Array.isArray(plan?.sections) || !Array.isArray(score?.parts))
    throw new InputError('Musical checkpoint is incomplete.');
  const byName = new Map();
  for (const part of score.parts) {
    if (!Number.isInteger(part.part_id) || !part.track_name || byName.has(part.track_name) ||
        !plan.cast[part.part_id - 1]?.id) throw new InputError('Checkpoint cast is ambiguous.');
    byName.set(part.track_name, { id: plan.cast[part.part_id - 1].id, count: part.note_count });
  }
  const midi = parseMidi(readFileSync(path.join(previousDirectory, checkpoint.file)));
  const notes = [];
  for (const track of midi.tracks) {
    if (!track.notes.length) continue;
    const part = byName.get(track.name);
    if (!part || part.count !== track.notes.length || track.notes.some(note => note.unterminated))
      throw new InputError('Provisional MIDI differs from accepted tracks.');
    for (const note of track.notes) notes.push({ instrument_id: part.id,
      start_beat: note.startBeat, duration_beats: note.durationBeats,
      pitch: note.pitch, velocity: note.velocity });
  }
  if (notes.length !== checkpoint.notes || byName.size !== checkpoint.parts)
    throw new InputError('Checkpoint MIDI note count does not match.');
  const blueprintFile = path.join(previousDirectory, 'accepted-blueprint.json');
  const blueprintJson = existsSync(blueprintFile) && statSync(blueprintFile).size <= 2 * 1024 * 1024
    ? readFileSync(blueprintFile, 'utf8') : null;
  if (blueprintJson && !readJson(blueprintFile))
    throw new InputError('El plan exacto del checkpoint no es JSON válido.');
  return { schema_version: 2, blueprint_json: blueprintJson,
    completed_units: Number.isInteger(checkpoint.completed_units)
      ? checkpoint.completed_units : Number.parseInt(stem.slice(8), 10),
    cast_ids: plan.cast.map(part => part.id),
    sections: plan.sections.map(section => ({ start_bar: section.start_bar, bars: section.bars })),
    notes };
}

function childEnvironment(dir) {
  // Local Studio advertises and checks its environment key. Force the worker to
  // use that same key instead of silently preferring a different VST credential
  // stored in Windows Credential Manager.
  const job = readJson(path.join(dir, 'job.json'));
  const automatic = (job?.automatic_recovery_attempt || 0) > 0;
  return workerEnvironmentFor(process.env, dir, dataRoot, automatic, experimentalProfile,
    job?.first_pass_audition === true, typeof job?.review_from_job === 'string',
    job?.phrase_architecture_experiment === true);
}

function startAutomaticRecovery(sourceId) {
  if (active) return;
  const source = jobDetails(sourceId);
  const hasExactBlueprint = existsSync(path.join(jobDirectory(sourceId), 'accepted-blueprint.json'));
  if (!canAutomaticallyRecover(source, hasExactBlueprint)) return;
  try {
    const request = automaticRecoveryRequest(source, sourceId);
    const resumeScore = resumeScoreFromJob(request);
    if (!resumeScore.blueprint_json) return;
    const id = randomUUID();
    const dir = path.join(jobsRoot, id);
    mkdirSync(dir, { recursive: false });
    writeFileSync(path.join(dir, 'local-origin.json'), JSON.stringify({ origin: source.origin }));
    request.resume_checkpoint_file = path.join(dir, 'resume-score.json');
    writeFileSync(request.resume_checkpoint_file, JSON.stringify(resumeScore));
    writeFileSync(path.join(dir, 'job.json'), JSON.stringify(request, null, 2));
    writeFileSync(path.join(jobDirectory(sourceId), 'recovery.json'), JSON.stringify({
      jobId: id, automatic: true, maxSeconds: AUTOMATIC_RECOVERY_MAX_SECONDS,
      estimatedUsdSoftCap: AUTOMATIC_RECOVERY_MAX_USD,
      note: 'El costo es estimado; una llamada ya iniciada puede superar el límite.'
    }));
    launch(id, dir);
  } catch (error) {
    appendFileSync(path.join(dataRoot, 'server-errors.jsonl'), JSON.stringify({
      id: randomUUID(), at: new Date().toISOString(), route: 'automatic-recovery',
      name: error.name, message: String(error.message || error).slice(0, 500)
    }) + '\n');
  }
}

function launch(id, dir) {
  const environment = childEnvironment(dir);
  if (experimentalProfile) writeFileSync(path.join(dir, 'experiment-context.json'), JSON.stringify({
    composerProfile: composerProfile.name,
    workerSha256: createHash('sha256').update(readFileSync(worker)).digest('hex'),
    workerPath: worker,
    aiFlags: Object.fromEntries(Object.entries(environment).filter(([key]) => key.startsWith('PULSO_AI_'))),
  }, null, 2));
  const child = spawn(worker, [path.join(dir, 'job.json'), dir], { env: environment, windowsHide: true, stdio: ['ignore', 'ignore', 'pipe'] });
  active = { id, child, cancelled: false };
  let diagnostic = '';
  child.stderr.on('data', chunk => { diagnostic = (diagnostic + chunk.toString()).slice(-4000); });
  child.on('error', error => {
    if (!existsSync(path.join(dir, 'error.json'))) writeFileSync(path.join(dir, 'error.json'), JSON.stringify({ error: `No se pudo iniciar el generador: ${error.message}` }));
    if (active?.id === id) active = null;
  });
  child.on('close', code => {
    if (code !== 0 && !existsSync(path.join(dir, 'error.json'))) {
      const message = active?.cancelled ? 'Generación cancelada por el usuario.' : `El generador terminó con código ${code}. ${diagnostic}`;
      writeFileSync(path.join(dir, 'error.json'), JSON.stringify({ error: message.slice(0, 1000) }));
    }
    const eligible = active?.id === id && !active.cancelled && code !== 0;
    if (active?.id === id) active = null;
    if (eligible) startAutomaticRecovery(id);
  });
}

function mutatingRequestAllowed(req) {
  const origin = req.headers.origin;
  return req.headers['x-pulso-local'] === '1' && (!origin || origin === `http://127.0.0.1:${port}` || origin === `http://localhost:${port}`);
}

function serveFile(res, file, type, cacheControl = 'no-store') {
  res.writeHead(200, { 'Content-Type': type, 'Cache-Control': cacheControl });
  createReadStream(file).pipe(res);
}

const server = http.createServer(async (req, res) => {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('Referrer-Policy', 'no-referrer');
  res.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");
  const url = new URL(req.url, `http://127.0.0.1:${port}`);
  const pathname = url.pathname;
  try {
    if (req.method === 'GET' && pathname === '/api/status') return json(res, 200, { workerReady: existsSync(worker), apiKeyConfigured: Boolean(process.env.OPENAI_API_KEY), activeJobId: active?.id || null, localOnly: true, editorialProfile: 'ai-only-v2', composerProfile: composerProfile.name, experimentalProfile,
      musicalProfile: experimentalProfile && process.env.PULSO_AI_GESTURE_SCORE_EXPERIMENT === '1'
        ? 'gesture-score' : experimentalProfile && process.env.PULSO_AI_EXPECTATION_LED_EXPERIMENT === '1'
        ? 'expectation-led' : experimentalProfile && process.env.PULSO_AI_MATERIAL_DIALOGUE_EXPERIMENT === '1'
        ? 'material-dialogue' : experimentalProfile && process.env.PULSO_AI_WHOLE_ARGUMENT_EXPERIMENT === '1'
        ? 'whole-argument' : experimentalProfile && process.env.PULSO_AI_CONSEQUENTIAL_DEVELOPMENT_EXPERIMENT === '1'
        ? 'consequential-development' : experimentalProfile && process.env.PULSO_AI_THEMATIC_GROUNDWORK_EXPERIMENT === '1'
        ? 'thematic-groundwork' : experimentalProfile && process.env.PULSO_AI_PHRASE_DEVELOPMENT_EXPERIMENT === '1'
        ? 'phrase-development' : experimentalProfile && process.env.PULSO_AI_PHRASE_PROSODY_EXPERIMENT === '1' &&
          process.env.PULSO_AI_THEMATIC_REHEARSAL_EXPERIMENT === '1'
        ? 'phrase-prosody-rehearsal' : experimentalProfile && process.env.PULSO_AI_PHRASE_PROSODY_EXPERIMENT === '1'
        ? 'phrase-prosody' : experimentalProfile && process.env.PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT === '1'
        ? 'thematic-subject' : experimentalProfile && process.env.PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT === '1'
        ? 'complete-dialogue' : experimentalProfile && process.env.PULSO_AI_ENSEMBLE_DISCOURSE_EXPERIMENT === '1'
        ? 'ensemble-discourse' : experimentalProfile && process.env.PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT === '1'
        ? 'connected-discourse' : 'shared-default' });
    if (req.method === 'GET' && pathname === '/api/jobs') return json(res, 200, { jobs: listJobs() });
    if (req.method === 'POST' && !mutatingRequestAllowed(req)) return json(res, 403, { error: 'Solicitud local no autorizada.' });
    if (req.method === 'POST' && (pathname === '/api/jobs' || pathname === '/api/auto/jobs')) {
      if (active) return json(res, 409, { error: 'Ya hay una composición en curso. Esperá a que termine o cancelala.' });
      if (!existsSync(worker)) return json(res, 503, { error: 'No se encontró el generador local. Revisá PULSO_LOCAL_WORKER_PATH.' });
      if (!process.env.OPENAI_API_KEY) return json(res, 503, { error: 'OPENAI_API_KEY no está configurada en este proceso.' });
      const request = validate(await bodyJson(req));
      const resumeScore = request.foreground_from_job ? foregroundScoreFromJob(request) :
        request.review_from_job ? resumeScoreFromJob(request, true) :
        request.resume_from_job ? resumeScoreFromJob(request) :
        request.blueprint_from_job ? blueprintOnlyScoreFromJob(request) : null;
      const id = randomUUID();
      const dir = path.join(jobsRoot, id);
      mkdirSync(dir, { recursive: false });
      writeFileSync(path.join(dir, 'local-origin.json'), JSON.stringify({ origin: originForRoute(pathname) }));
      if (resumeScore) {
        request.resume_checkpoint_file = path.join(dir, 'resume-score.json');
        writeFileSync(request.resume_checkpoint_file, JSON.stringify(resumeScore));
      }
      writeFileSync(path.join(dir, 'job.json'), JSON.stringify(request, null, 2));
      launch(id, dir);
      return json(res, 202, { job: jobDetails(id) });
    }
    const detailMatch = pathname.match(/^\/api\/jobs\/([0-9a-f-]+)$/i);
    if (req.method === 'GET' && detailMatch) {
      const job = jobDetails(detailMatch[1]);
      return job ? json(res, 200, { job }) : json(res, 404, { error: 'Composición no encontrada.' });
    }
    const traceMatch = pathname.match(/^\/api\/jobs\/([0-9a-f-]+)\/trace$/i);
    if (req.method === 'GET' && traceMatch) {
      const job = jobDetails(traceMatch[1]);
      if (!job) return json(res, 404, { error: 'Composición no encontrada.' });
      if (url.searchParams.get('download') === '1') res.setHeader('Content-Disposition', `attachment; filename="pulso-trace-${job.id}.json"`);
      return json(res, 200, { jobId: job.id, request: job.request, ...traceFor(job.id, job.request.created_at), summary: job.telemetry });
    }
    const cancelMatch = pathname.match(/^\/api\/jobs\/([0-9a-f-]+)\/cancel$/i);
    if (req.method === 'POST' && cancelMatch) {
      if (active?.id !== cancelMatch[1]) return json(res, 409, { error: 'La composición ya no está activa.' });
      active.cancelled = true;
      writeFileSync(path.join(jobDirectory(active.id), 'cancelled.json'), JSON.stringify({ cancelled_at: new Date().toISOString() }));
      active.child.kill();
      return json(res, 202, { message: 'Se solicitó detener el proceso local. Una solicitud remota ya aceptada podría seguir consumiendo créditos.' });
    }
    const fileMatch = pathname.match(/^\/api\/jobs\/([0-9a-f-]+)\/files\/([^/]+)$/i);
    if (req.method === 'GET' && fileMatch) {
      const job = jobDetails(fileMatch[1]);
      if (!job) return json(res, 404, { error: 'Composición no encontrada.' });
      const filename = decodeURIComponent(fileMatch[2]);
      const allowed = new Set(['job.json', 'manifest.json', 'checkpoint.json', 'diagnostic.json', job.compositionAudit && 'composition-audit.json', job.phraseAudit && 'phrase-audit.json', job.checkpoint?.file, job.diagnostic?.file, job.diagnostic?.planFile, job.diagnostic?.auditFile, job.manifest?.planFile, job.manifest?.phraseArchitectureFile, job.manifest?.auditFile, job.manifest?.fullFile, job.manifest?.comparisonFile, ...(job.manifest?.tracks || []).map(track => track.filename)]);
      if (experimentalProfile && job.manifest?.thematic_rehearsal)
        for (const name of ['partial-01.mid', 'partial-02.mid', 'thematic-rehearsal.json']) allowed.add(name);
      if (!allowed.has(filename) || !/^[a-zA-Z0-9._-]+$/.test(filename)) return json(res, 404, { error: 'Archivo no disponible.' });
      const file = path.join(jobDirectory(job.id), filename);
      if (!existsSync(file)) return json(res, 404, { error: 'Archivo no disponible.' });
      res.setHeader('Content-Disposition', `attachment; filename="${filename}"`);
      return serveFile(res, file, filename.endsWith('.mid') ? 'audio/midi' : 'application/json; charset=utf-8');
    }
    if (req.method === 'GET' && pathname === '/') return serveFile(res, path.join(here, 'index.html'), 'text/html; charset=utf-8');
    if (req.method === 'GET' && pathname === '/style.css') return serveFile(res, path.join(here, 'style.css'), 'text/css; charset=utf-8');
    if (req.method === 'GET' && pathname === '/telemetry.css') return serveFile(res, path.join(here, 'telemetry.css'), 'text/css; charset=utf-8');
    if (req.method === 'GET' && pathname === '/app.js') return serveFile(res, path.join(here, 'app.js'), 'text/javascript; charset=utf-8');
    if (req.method === 'GET' && pathname === '/midi.mjs') return serveFile(res, path.join(shared, 'midi.mjs'), 'text/javascript; charset=utf-8');
    if (req.method === 'GET' && pathname === '/audio-engine.mjs') return serveFile(res, path.join(shared, 'audio-engine.mjs'), 'text/javascript; charset=utf-8');
    if (req.method === 'GET' && pathname === '/production-audio.mjs') return serveFile(res, path.join(shared, 'production-audio.mjs'), 'text/javascript; charset=utf-8');
    if (req.method === 'GET' && pathname === '/sound-palette.mjs') return serveFile(res, path.join(shared, 'sound-palette.mjs'), 'text/javascript; charset=utf-8');
    if (req.method === 'GET' && /^\/piano\/INSPiano_Vel03_(?:A0|C[1-8]|Fs[1-7])_01\.ogg$/.test(pathname))
      return serveFile(res, path.join(here, '../frontend/public', pathname), 'audio/ogg', 'public, max-age=31536000, immutable');
    return json(res, 404, { error: 'Ruta no encontrada.' });
  } catch (error) {
    const isInput = error instanceof InputError;
    if (isInput) return json(res, 400, { error: error.message });
    const diagnosticId = randomUUID();
    const safeMessage = String(error.message || error.name || 'Unknown error')
      .replace(/sk-(?:proj-)?[A-Za-z0-9_-]+/g, '[redacted]');
    try {
      appendFileSync(path.join(dataRoot, 'server-errors.jsonl'), JSON.stringify({
        id: diagnosticId, at: new Date().toISOString(), method: req.method,
        route: pathname, name: error.name, message: safeMessage,
      }) + '\n');
    } catch { /* The response still carries the diagnostic ID. */ }
    return json(res, 500, { error: `Error interno del estudio local (diagnóstico ${diagnosticId}).` });
  }
});

server.listen(port, '127.0.0.1', () => {
  process.stdout.write(`PULSO Local Studio: http://127.0.0.1:${port}\n`);
});
