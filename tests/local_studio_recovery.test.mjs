import test from 'node:test';
import assert from 'node:assert/strict';
import { canAutomaticallyRecover, automaticRecoveryRequest,
  AUTOMATIC_RECOVERY_MAX_SECONDS, AUTOMATIC_RECOVERY_MAX_USD,
  MAX_CHECKPOINT_RECOVERIES } from '../web/local-studio/recovery-policy.mjs';

const eligible = () => ({ state: 'failed', checkpoint: { file: 'partial-07.mid' },
  manifest: null, error: 'Final MIDI review requires a focused musical revision',
  request: { prompt: 'A coherent song', duration_seconds: 300, bpm: 120,
    seed: '123', ai_sovereign: true, proof_mode: false,
    resume_from_job: null } });

test('an exact-checkpoint failure may recover within the same three-retry bound as Cloud', () => {
  const source = eligible();
  assert.equal(canAutomaticallyRecover(source, true), true);
  const retry = automaticRecoveryRequest(source, 'source-id', new Date('2026-10-11T00:00:00Z'));
  assert.equal(retry.resume_from_job, 'source-id');
  assert.equal(retry.automatic_recovery_attempt, 1);
  assert.equal(retry.created_at, '2026-10-11T00:00:00.000Z');
  assert.equal(source.request.resume_from_job, null);
  assert.equal(AUTOMATIC_RECOVERY_MAX_SECONDS, 600);
  assert.equal(AUTOMATIC_RECOVERY_MAX_USD, 20);
  assert.equal(MAX_CHECKPOINT_RECOVERIES, 3);
  const second = { ...source, request: retry };
  assert.equal(canAutomaticallyRecover(second, true), true);
  const next = automaticRecoveryRequest(second, 'next-id');
  assert.equal(next.automatic_recovery_attempt, 2);
  assert.equal(next.recovery_source_job, 'source-id');
  const third = automaticRecoveryRequest({ ...source, request: next }, 'third-id');
  assert.equal(third.automatic_recovery_attempt, 3);
  assert.equal(canAutomaticallyRecover({ ...source, request: third }, true), false);
});

test('never retries without exact plan and accepted MIDI', () => {
  assert.equal(canAutomaticallyRecover(eligible(), false), false);
  const withoutMidi = eligible(); withoutMidi.checkpoint = null;
  assert.equal(canAutomaticallyRecover(withoutMidi, true), false);
  const complete = eligible(); complete.manifest = { fullFile: 'full-song.mid' };
  assert.equal(canAutomaticallyRecover(complete, true), false);
});

test('never loops or retries cancelled, proof or unaffordable requests', () => {
  const retry = eligible(); retry.request.automatic_recovery_attempt = 3;
  assert.equal(canAutomaticallyRecover(retry, true), false);
  const resumed = eligible(); resumed.request.resume_from_job = 'prior';
  assert.equal(canAutomaticallyRecover(resumed, true), false);
  const cancelled = eligible(); cancelled.state = 'cancelled';
  assert.equal(canAutomaticallyRecover(cancelled, true), false);
  const proof = eligible(); proof.request.proof_mode = true;
  assert.equal(canAutomaticallyRecover(proof, true), false);
  const quota = eligible(); quota.error = 'Este proyecto no tiene créditos disponibles';
  assert.equal(canAutomaticallyRecover(quota, true), false);
});
