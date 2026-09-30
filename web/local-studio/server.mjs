import http from 'node:http';
import { spawn } from 'node:child_process';
import { randomBytes, randomUUID } from 'node:crypto';
import { existsSync, createReadStream, mkdirSync, readFileSync, readdirSync, statSync, writeFileSync, appendFileSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const here = path.dirname(fileURLToPath(import.meta.url));
const root = path.resolve(here, '..', '..');
const worker = process.env.PULSO_LOCAL_WORKER_PATH || path.join(root, 'build-cloud', 'Release', 'pulso_cloud_worker.exe');
const dataRoot = process.env.PULSO_LOCAL_DATA_DIR || path.join(process.env.LOCALAPPDATA || os.tmpdir(), 'Pulso', 'LocalStudio');
const jobsRoot = path.join(dataRoot, 'jobs');
const port = Number(process.env.PULSO_LOCAL_PORT || 4177);
if (!Number.isInteger(port) || port < 1 || port > 65535) throw new Error('Invalid PULSO_LOCAL_PORT');
mkdirSync(jobsRoot, { recursive: true });

let active = null;
const jobIdPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;
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
  if (!call.usage_available || call.model !== 'gpt-5.6-terra') return null;
  const input = Number(call.input_tokens) || 0;
  const cached = Number(call.cached_input_tokens) || 0;
  const writes = Number(call.cache_write_tokens) || 0;
  const output = Number(call.output_tokens) || 0;
  const ordinary = Math.max(0, input - cached - writes);
  const longContext = input > 272000;
  return ((ordinary * 2 + cached * 0.2 + writes * 2.5) * (longContext ? 2 : 1) + output * 12 * (longContext ? 1.5 : 1)) / 1000000;
}

function traceFor(id, createdAt, endedAt = Date.now()) {
  const dir = jobDirectory(id);
  const apiCalls = readJsonLines(path.join(dir, 'api-events.jsonl')).map(call => ({ ...call, estimatedUsd: estimatedUsd(call) }));
  const progressEvents = readJsonLines(path.join(dir, 'progress-events.jsonl'));
  const totals = { calls: apiCalls.length, callsWithUsage: 0, pricedCalls: 0, estimatedUsd: 0, priceAsOf: '2026-09-29', priceSource: 'https://developers.openai.com/api/docs/models/gpt-5.6-terra', inputTokens: 0, cachedInputTokens: 0, cacheWriteTokens: 0, outputTokens: 0, reasoningTokens: 0, totalTokens: 0, timedOutCalls: 0, incompleteCalls: 0, failedCalls: 0, recoveryEvents: progressEvents.filter(event => event.stage === 'recovery').length, summedApiMs: 0, model: apiCalls.find(call => call.model)?.model || null, effort: apiCalls.find(call => call.effort)?.effort || null };
  for (const call of apiCalls) {
    totals.summedApiMs += Number(call.elapsed_ms) || 0;
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

function jobDirectory(id) {
  return jobIdPattern.test(id) ? path.join(jobsRoot, id) : null;
}

function jobDetails(id) {
  const dir = jobDirectory(id);
  if (!dir) return null;
  const request = readJson(path.join(dir, 'job.json'));
  if (!request) return null;
  const manifest = readJson(path.join(dir, 'manifest.json'));
  // MIDI sidecars are produced by the existing exporter. Read them server-side
  // so older jobs also get sound-role metadata without rebuilding the worker.
  if (manifest?.tracks) manifest.tracks = manifest.tracks.map(track => {
    if (!/^track-[0-9]+\.mid$/i.test(track.filename || '')) return track;
    const sidecar = readJson(path.join(dir, track.filename.replace(/\.mid$/i, '.pulso.json')));
    const part = sidecar?.parts?.[0];
    return part ? { ...track, instrument: {
      catalog_id: part.catalog_id, department: part.department,
      source_voice: part.source_voice, role: part.role,
    } } : track;
  });
  const checkpoint = readJson(path.join(dir, 'checkpoint.json'));
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
  return { id, request, state, stage: stages[progress?.stage] || progress?.stage || '', completed: progress?.completed || 0, total: progress?.total || 0, manifest, checkpoint, telemetry: trace.summary, error: state === 'cancelled' ? null : explainJobFailure(failure?.error) || (state === 'interrupted' ? 'El proceso se interrumpió. Los archivos existentes se conservaron.' : null) };
}

function listJobs() {
  return readdirSync(jobsRoot, { withFileTypes: true })
    .filter(entry => entry.isDirectory() && jobIdPattern.test(entry.name))
    .map(entry => jobDetails(entry.name))
    .filter(Boolean)
    .sort((a, b) => b.request.created_at.localeCompare(a.request.created_at))
    .slice(0, 50);
}

async function bodyJson(req) {
  let raw = '';
  for await (const chunk of req) {
    raw += chunk;
    if (raw.length > 8192) throw new Error('La solicitud es demasiado grande.');
  }
  try { return JSON.parse(raw); } catch { throw new Error('La solicitud no es JSON válido.'); }
}

function validate(input) {
  const prompt = typeof input.prompt === 'string' ? input.prompt.trim() : '';
  const duration = input.duration_seconds;
  const bpm = input.bpm;
  const behavior = input.behavior || 'adaptive';
  const renderMode = input.render_mode || 'ai_sovereign';
  const suppliedSeed = input.seed === undefined || input.seed === '' ? null : String(input.seed);
  if (!prompt || prompt.length > 600) throw new Error('La idea debe contener entre 1 y 600 caracteres.');
  if (!Number.isInteger(duration) || duration < 30 || duration > 900) throw new Error('La duración debe estar entre 30 y 900 segundos.');
  if (!Number.isFinite(bpm) || bpm < 60 || bpm > 180) throw new Error('El tempo debe estar entre 60 y 180 BPM.');
  if (!['adaptive', 'hypnotic', 'narrative'].includes(behavior)) throw new Error('El enfoque musical no es válido.');
  if (!['ai_sovereign', 'standard'].includes(renderMode)) throw new Error('El modo de render no es válido.');
  if (suppliedSeed !== null && (!/^[1-9][0-9]{0,18}$/.test(suppliedSeed) || BigInt(suppliedSeed) > 9223372036854775807n)) throw new Error('La semilla debe ser un entero positivo de 64 bits.');
  const seed = suppliedSeed || String((randomBytes(8).readBigUInt64LE() & 9223372036854775807n) || 1n);
  return { prompt, duration_seconds: duration, bpm, behavior, seed, ai_sovereign: renderMode === 'ai_sovereign', created_at: new Date().toISOString() };
}

function childEnvironment(dir) {
  const keys = ['OPENAI_API_KEY', 'PATH', 'Path', 'SystemRoot', 'WINDIR', 'TEMP', 'TMP', 'USERPROFILE', 'APPDATA', 'LOCALAPPDATA', 'ProgramData'];
  // Local Studio advertises and checks its environment key. Force the worker to
  // use that same key instead of silently preferring a different VST credential
  // stored in Windows Credential Manager.
  return { ...Object.fromEntries(keys.filter(key => process.env[key]).map(key => [key, process.env[key]])), PULSO_DISABLE_SECURE_CREDENTIALS: '1', PULSO_TRACE_PATH: path.join(dir, 'api-events.jsonl') };
}

function launch(id, dir) {
  const child = spawn(worker, [path.join(dir, 'job.json'), dir], { env: childEnvironment(dir), windowsHide: true, stdio: ['ignore', 'ignore', 'pipe'] });
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
    if (active?.id === id) active = null;
  });
}

function mutatingRequestAllowed(req) {
  const origin = req.headers.origin;
  return req.headers['x-pulso-local'] === '1' && (!origin || origin === `http://127.0.0.1:${port}` || origin === `http://localhost:${port}`);
}

function serveFile(res, file, type) {
  res.writeHead(200, { 'Content-Type': type, 'Cache-Control': 'no-store' });
  createReadStream(file).pipe(res);
}

const server = http.createServer(async (req, res) => {
  res.setHeader('X-Content-Type-Options', 'nosniff');
  res.setHeader('Referrer-Policy', 'no-referrer');
  res.setHeader('Content-Security-Policy', "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self' data:; connect-src 'self'; base-uri 'none'; frame-ancestors 'none'");
  const url = new URL(req.url, `http://127.0.0.1:${port}`);
  const pathname = url.pathname;
  try {
    if (req.method === 'GET' && pathname === '/api/status') return json(res, 200, { workerReady: existsSync(worker), apiKeyConfigured: Boolean(process.env.OPENAI_API_KEY), activeJobId: active?.id || null, localOnly: true });
    if (req.method === 'GET' && pathname === '/api/jobs') return json(res, 200, { jobs: listJobs() });
    if (req.method === 'POST' && !mutatingRequestAllowed(req)) return json(res, 403, { error: 'Solicitud local no autorizada.' });
    if (req.method === 'POST' && pathname === '/api/jobs') {
      if (active) return json(res, 409, { error: 'Ya hay una composición en curso. Esperá a que termine o cancelala.' });
      if (!existsSync(worker)) return json(res, 503, { error: 'No se encontró el generador local. Revisá PULSO_LOCAL_WORKER_PATH.' });
      if (!process.env.OPENAI_API_KEY) return json(res, 503, { error: 'OPENAI_API_KEY no está configurada en este proceso.' });
      const request = validate(await bodyJson(req));
      const id = randomUUID();
      const dir = jobDirectory(id);
      mkdirSync(dir, { recursive: false });
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
      const allowed = new Set(['job.json', 'manifest.json', 'checkpoint.json', job.checkpoint?.file, job.manifest?.planFile, job.manifest?.fullFile, job.manifest?.comparisonFile, ...(job.manifest?.tracks || []).map(track => track.filename)]);
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
    if (req.method === 'GET' && pathname === '/midi.mjs') return serveFile(res, path.join(here, 'midi.mjs'), 'text/javascript; charset=utf-8');
    if (req.method === 'GET' && pathname === '/audio-engine.mjs') return serveFile(res, path.join(here, 'audio-engine.mjs'), 'text/javascript; charset=utf-8');
    return json(res, 404, { error: 'Ruta no encontrada.' });
  } catch (error) {
    const isInput = /solicitud|duración|tempo|semilla|idea|enfoque/i.test(error.message);
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
