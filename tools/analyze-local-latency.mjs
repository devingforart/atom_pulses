import { readFileSync, readdirSync, existsSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const projectRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const jobsRoot = process.env.PULSO_LOCAL_DATA_DIR
  ? path.join(process.env.PULSO_LOCAL_DATA_DIR, 'jobs')
  : path.join(projectRoot, 'build', 'local-studio-data', 'jobs');

export function summarizeLatency(apiCalls, progress, job = {}) {
  const phases = new Map();
  for (const call of apiCalls.filter(event => event.event === 'api_call')) {
    const phase = call.phase || 'unknown';
    const row = phases.get(phase) || { phase, calls: 0, apiMs: 0, inputTokens: 0,
      cachedInputTokens: 0, outputTokens: 0, failed: 0 };
    row.calls++;
    row.apiMs += Number(call.elapsed_ms) || 0;
    row.inputTokens += Number(call.input_tokens) || 0;
    row.cachedInputTokens += Number(call.cached_input_tokens) || 0;
    row.outputTokens += Number(call.output_tokens) || 0;
    if (!call.connected || call.timed_out || call.cancelled ||
        (Number(call.http_status) < 200 || Number(call.http_status) >= 300) ||
        ['incomplete', 'failed', 'cancelled'].includes(call.remote_status)) row.failed++;
    phases.set(phase, row);
  }
  const writingStarts = progress.filter(event => event.stage === 'writing' &&
    /^instrument block \d+\/\d+/.test(event.detail || ''));
  const blocks = new Map();
  for (const event of writingStarts) {
    const match = event.detail.match(/^instrument block (\d+)\/(\d+) - (\d+) parts?/);
    if (!match) continue;
    const number = Number(match[1]);
    if (!blocks.has(number)) blocks.set(number, { number, plannedParts: Number(match[3]),
      startedAt: event.at, retries: 0 });
    else blocks.get(number).retries++;
  }
  const groupedSplits = progress.filter(event => event.stage === 'recovery' &&
    /separating discordant voices/.test(event.detail || '')).length;
  const compactRepairs = progress.filter(event => event.stage === 'recovery' &&
    /AI refining .* (?:source-cell attacks|chord attacks)/.test(event.detail || '')).length;
  const incompleteParts = progress.filter(event => event.stage === 'recovery' &&
    /granular recovery|missing.*part/i.test(event.detail || '')).length;
  const apiMs = [...phases.values()].reduce((sum, row) => sum + row.apiMs, 0);
  const writingEnd = progress.find(event => event.stage === 'validation' &&
    /assembling and validating all authored blocks/.test(event.detail || ''))?.at;
  const starts = [...blocks.values()].sort((a, b) => Date.parse(a.startedAt) - Date.parse(b.startedAt));
  for (const block of starts) { block.apiCalls = 0; block.apiMs = 0; }
  for (const call of apiCalls.filter(event => event.event === 'api_call' && event.at &&
      (!writingEnd || Date.parse(event.at) < Date.parse(writingEnd)))) {
    const owner = starts.findLast(block => Date.parse(block.startedAt) <= Date.parse(call.at));
    if (owner) { owner.apiCalls++; owner.apiMs += Number(call.elapsed_ms) || 0; }
  }
  return { id: job.id || null, state: job.state || null,
    requestedSeconds: job.request?.duration_seconds || null,
    wallMs: Number(job.telemetry?.wallMs) || 0, apiMs,
    plannedBlocks: blocks.size, groupedSplits, compactRepairs, incompleteParts,
    blocks: starts.sort((a, b) => a.number - b.number),
    phases: [...phases.values()].sort((a, b) => b.apiMs - a.apiMs) };
}

function jsonLines(file) {
  if (!existsSync(file)) return [];
  return readFileSync(file, 'utf8').split(/\r?\n/).filter(Boolean).map(line => JSON.parse(line));
}

function main() {
  const prefix = process.argv[2];
  if (!prefix || !/^[0-9a-f-]{8,36}$/i.test(prefix))
    throw new Error('Usage: node tools/analyze-local-latency.mjs <job-id-or-prefix>');
  const matches = readdirSync(jobsRoot, { withFileTypes: true })
    .filter(entry => entry.isDirectory() && entry.name.startsWith(prefix));
  if (matches.length !== 1) throw new Error(`Expected one job for ${prefix}; found ${matches.length}`);
  const dir = path.join(jobsRoot, matches[0].name);
  const readJson = name => existsSync(path.join(dir, name))
    ? JSON.parse(readFileSync(path.join(dir, name), 'utf8')) : null;
  const request = readJson('job.json');
  const progress = readJson('progress.json');
  const progressEvents = jsonLines(path.join(dir, 'progress-events.jsonl'));
  const lastAt = progress?.state === 'running'
    ? new Date().toISOString() : progressEvents.at(-1)?.at;
  const wallMs = lastAt ? Date.parse(lastAt) - Date.parse(request.created_at) : 0;
  const report = summarizeLatency(jsonLines(path.join(dir, 'api-events.jsonl')),
    progressEvents,
    { id: matches[0].name, state: progress?.state, request,
      telemetry: { wallMs } });
  process.stdout.write(`${JSON.stringify(report, null, 2)}\n`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url)) main();
