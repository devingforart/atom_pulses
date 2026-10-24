import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { comparisonStore, criteria, evaluationReport, evaluationStore,
  publicComparison, validateEvaluation } from '../web/local-studio/evaluation-store.mjs';

const idA = '11111111-1111-4111-8111-111111111111';
const idB = '22222222-2222-4222-8222-222222222222';
const complete = { scores: Object.fromEntries(Object.keys(criteria).map(key => [key, 3])),
  scope: 'full', listening_mode: 'piano', would_use: 'no', confidence: 'high',
  problems: ['sin_idea', 'final_abrupto'], observation: 'No desarrolla la pregunta.',
  best_moment: 'compás 4', worst_moment: 'compás 28' };

test('evaluation requires every musical score and rejects unknown problem tags', () => {
  assert.equal(validateEvaluation(complete).scores.climax, 3);
  const missing = { ...complete, scores: { ...complete.scores } };
  delete missing.scores.ending;
  assert.throws(() => validateEvaluation(missing), /Puntuá Cierre/);
  assert.throws(() => validateEvaluation({ ...complete, problems: ['inventado'] }),
    /problemas reconocidos/);
});

test('ratings persist independently of MIDI and report joins subjective and technical evidence', () => {
  const root = mkdtempSync(path.join(os.tmpdir(), 'pulso-evaluation-'));
  try {
    const ratings = evaluationStore(root);
    const pairs = comparisonStore(root);
    ratings.save(idA, complete);
    assert.equal(evaluationStore(root).get(idA).worst_moment, 'compás 28');
    const pair = pairs.create(idA, idB);
    assert.equal(publicComparison(pair).sides[0], 'A');
    assert.equal(JSON.stringify(publicComparison(pair)).includes(idA), false);
    assert.equal(JSON.stringify(publicComparison(pair)).includes(idB), false);
    assert.throws(() => pairs.vote(pair.id, { winner: 'C' }), /preferencia/);
    const voted = pairs.vote(pair.id, { winner: 'B', scope: 'full',
      listening_mode: 'piano', reason: 'B desarrolla la idea' });
    assert.equal(publicComparison(voted).sides.B, pair.sides.B);
    assert.throws(() => pairs.vote(pair.id, { winner: 'A' }), /ya fue votada/);
    const job = { request: { prompt: 'Prueba', behavior: 'club', first_pass_audition: true,
      duration_seconds: 180, seed: '77', created_at: '2026-10-23T12:00:00Z' },
    manifest: { title: 'Ensayo', editorial: { narrative_ready: true,
      soundscape_ready: false } }, compositionAudit: { exactRepeatedEightBarRatio: 0.5 },
    telemetry: { estimatedUsd: 0.12 } };
    const report = evaluationReport(ratings.all(), pairs.all(), id => id === idA ? job : null);
    assert.equal(report.evaluated_works, 1);
    assert.equal(report.would_use.no, 1);
    assert.equal(report.criterion_averages.idea, 3);
    assert.equal(report.problem_counts.sin_idea, 1);
    assert.equal(report.by_editorial_mode.first_pass.count, 1);
    assert.equal(report.works[0].audit.soundscape_ready, false);
    assert.equal(report.blind_comparisons.length, 1);
  } finally { rmSync(root, { recursive: true, force: true }); }
});
