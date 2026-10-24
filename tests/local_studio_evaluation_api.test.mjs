import test from 'node:test';
import assert from 'node:assert/strict';
import { spawn } from 'node:child_process';
import { mkdtempSync, mkdirSync, rmSync, writeFileSync } from 'node:fs';
import net from 'node:net';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { criteria, previousComparableEvaluation } from '../web/local-studio/evaluation-store.mjs';
import { singleTrackMidi } from '../web/shared/midi.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
const serverFile = path.resolve(here, '../web/local-studio/server.mjs');
const ids = ['11111111-1111-4111-8111-111111111111',
  '22222222-2222-4222-8222-222222222222'];
const request = { prompt: 'Mismo encargo', duration_seconds: 60, bpm: 120,
  behavior: 'adaptive', seed: '7', created_at: '2026-10-23T12:00:00.000Z' };
const midi = singleTrackMidi({ division: 96, beatsPerBar: 4, lengthBeats: 4,
  tempos: [{ tick: 0, microseconds: 500000 }] }, [{ notes: [
    { startBeat: 0, durationBeats: 1, pitch: 60, velocity: 90 } ] }]);
const port = () => new Promise((resolve, reject) => {
  const probe = net.createServer();
  probe.on('error', reject);
  probe.listen(0, '127.0.0.1', () => {
    const value = probe.address().port;
    probe.close(() => resolve(value));
  });
});
const waitFor = async url => {
  for (let i = 0; i < 60; i++) {
    try { const result = await fetch(url); if (result.ok) return; } catch { /* Starting. */ }
    await new Promise(resolve => setTimeout(resolve, 50));
  }
  throw new Error('Local Studio did not start');
};

test('evaluation reference uses the latest older comparable work only', () => {
  const make = (id, created_at, prompt = request.prompt) => ({ id,
    request: { ...request, prompt, created_at }, manifest: { title: id } });
  const current = make('current', '2026-10-23T15:00:00.000Z');
  const jobs = [make('old', '2026-10-23T12:00:00.000Z'),
    make('latest', '2026-10-23T14:00:00.000Z'),
    make('future', '2026-10-23T16:00:00.000Z'),
    make('different', '2026-10-23T14:30:00.000Z', 'Otro encargo')];
  const evaluations = jobs.map(job => ({ job_id: job.id, scores: { climax: 3 } }));
  const lookup = id => jobs.find(job => job.id === id);
  assert.equal(previousComparableEvaluation(current, evaluations, lookup).job_id, 'latest');
  assert.equal(previousComparableEvaluation({ ...current,
    request: { ...current.request, seed: 'other' } }, evaluations, lookup), null);
});

test('local musical evaluation and blind A/B work over HTTP without revealing sides before voting', async () => {
  const root = mkdtempSync(path.join(os.tmpdir(), 'pulso-evaluation-api-'));
  const listenPort = await port();
  for (const [index, id] of ids.entries()) {
    const directory = path.join(root, 'jobs', id);
    mkdirSync(directory, { recursive: true });
    writeFileSync(path.join(directory, 'job.json'), JSON.stringify({ ...request,
      created_at: `2026-10-23T${12 + index}:00:00.000Z` }));
    writeFileSync(path.join(directory, 'local-origin.json'), JSON.stringify({ origin: 'personal' }));
    writeFileSync(path.join(directory, 'full-song.mid'), midi);
    writeFileSync(path.join(directory, 'manifest.json'), JSON.stringify({
      title: `Obra ${index + 1}`, fullFile: 'full-song.mid', tracks: [],
      editorial: { technical_ready: true, narrative_ready: true } }));
  }
  const child = spawn(process.execPath, [serverFile], { windowsHide: true,
    env: { ...process.env, PULSO_LOCAL_DATA_DIR: root,
      PULSO_LOCAL_PORT: String(listenPort), PULSO_LOCAL_WORKER_PATH: path.join(root, 'missing-worker') },
    stdio: ['ignore', 'pipe', 'pipe'] });
  const base = `http://127.0.0.1:${listenPort}`;
  const send = async (route, method, payload, header = true) => {
    const response = await fetch(base + route, { method,
      headers: { 'Content-Type': 'application/json', ...(header ? { 'X-Pulso-Local': '1' } : {}) },
      body: JSON.stringify(payload) });
    return { status: response.status, body: await response.json() };
  };
  try {
    await waitFor(base + '/api/status');
    const bank = await (await fetch(base + '/api/benchmarks')).json();
    assert.ok(bank.cases.length >= 5);
    const evaluation = { scores: Object.fromEntries(Object.keys(criteria).map(key => [key, 4])),
      scope: 'full', listening_mode: 'piano', would_use: 'maybe', confidence: 'high',
      problems: ['climax_plano'], observation: 'Clímax poco desarrollado.',
      best_moment: '0:20', worst_moment: '0:50' };
    assert.equal((await send(`/api/evaluations/${ids[0]}`, 'PUT', evaluation, false)).status, 403);
    assert.equal((await send(`/api/evaluations/${ids[0]}`, 'PUT', evaluation)).status, 200);
    const saved = await (await fetch(base + `/api/evaluations/${ids[0]}`)).json();
    assert.equal(saved.evaluation.scores.climax, 4);
    assert.equal(saved.reference, null);
    const next = await (await fetch(base + `/api/evaluations/${ids[1]}`)).json();
    assert.equal(next.evaluation, null);
    assert.equal(next.reference.job_id, ids[0]);
    assert.equal(next.reference.evaluation.scores.climax, 4);
    const created = await send('/api/comparisons', 'POST',
      { first_job_id: ids[0], second_job_id: ids[1] });
    assert.equal(created.status, 201);
    assert.equal(JSON.stringify(created.body).includes(ids[0]), false);
    assert.equal(JSON.stringify(created.body).includes(ids[1]), false);
    const pairId = created.body.comparison.id;
    for (const side of ['A', 'B']) {
      const response = await fetch(`${base}/api/comparisons/${pairId}/sides/${side}.mid`);
      assert.equal(response.status, 200);
      assert.equal(response.headers.get('content-type'), 'audio/midi');
    }
    const voted = await send(`/api/comparisons/${pairId}/vote`, 'POST',
      { winner: 'A', scope: 'full', listening_mode: 'piano', reason: 'Mejor cierre.' });
    assert.equal(voted.status, 200);
    assert.ok(ids.includes(voted.body.comparison.sides.A));
    const report = await (await fetch(base + '/api/evaluations/report')).json();
    assert.equal(report.evaluated_works, 1);
    assert.equal(report.blind_comparisons.length, 1);
    assert.equal(report.criterion_averages.climax, 4);
  } finally {
    child.kill();
    await new Promise(resolve => child.once('exit', resolve));
    rmSync(root, { recursive: true, force: true });
  }
});
