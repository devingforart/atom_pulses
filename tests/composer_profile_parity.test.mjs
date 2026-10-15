import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { composerProfile, defaultEditorialSettings,
  workerEnvironmentFor } from '../web/local-studio/composer-environment.mjs';
import { canAutomaticallyRecover } from '../web/local-studio/recovery-policy.mjs';

test('local production worker uses exactly the shared Cloud composer profile', () => {
  const source = { OPENAI_API_KEY: 'test-only', PATH: '/bin',
    PULSO_AI_FAST_MODE: '1', PULSO_AI_EXPERIMENT_MODEL: 'gpt-6-luna',
    PULSO_AI_MELODIC_EXPERIMENT: '1',
    PULSO_AI_GLOBAL_NARRATIVE_EXPERIMENT: '1',
    PULSO_AI_NARRATIVE_FULL_SCENE: '1',
    PULSO_AI_RHYTHMIC_ARC_EXPERIMENT: '1',
    PULSO_AI_NARRATIVE_FINALE_EXPERIMENT: '1',
    PULSO_AI_NARRATIVE_FOCUS: 'example' };
  const env = workerEnvironmentFor(source, '/jobs/one', '/data', false, false);
  for (const [key, value] of Object.entries(composerProfile.workerEnvironment))
    assert.equal(env[key], value, key);
  assert.equal(env.OPENAI_API_KEY, 'test-only');
  assert.equal(env.PULSO_TRACE_PATH, path.join('/jobs/one', 'api-events.jsonl'));
  assert.equal(env.PULSO_AI_FAST_MODE, undefined);
  assert.equal(env.PULSO_AI_EXPERIMENT_MODEL, undefined);
  assert.equal(env.PULSO_AI_GLOBAL_NARRATIVE_EDITOR, '1');
  assert.equal(env.PULSO_AI_MELODIC_EXPERIMENT, undefined);
  assert.equal(env.PULSO_AI_GLOBAL_NARRATIVE_EXPERIMENT, undefined);
  assert.equal(env.PULSO_AI_NARRATIVE_FULL_SCENE, undefined);
  assert.equal(env.PULSO_AI_RHYTHMIC_ARC_EXPERIMENT, undefined);
  assert.equal(env.PULSO_AI_NARRATIVE_FINALE_EXPERIMENT, undefined);
  assert.equal(env.PULSO_AI_NARRATIVE_FOCUS, undefined);
  assert.equal(env.PULSO_AI_RESPONSE_CACHE_DIR, undefined);
  assert.equal(env.PULSO_AI_FIRST_PASS_AUDITION, undefined);
});

test('first-pass audition is an explicit per-job local override, never inherited from process environment', () => {
  const source = { PULSO_AI_FIRST_PASS_AUDITION: '1' };
  const normal = workerEnvironmentFor(source, '/jobs/normal', '/data', false, false);
  const audition = workerEnvironmentFor(source, '/jobs/audition', '/data', false, false, true);
  assert.equal(normal.PULSO_AI_FIRST_PASS_AUDITION, undefined);
  assert.equal(audition.PULSO_AI_FIRST_PASS_AUDITION, '1');
  const experimentalAudition = workerEnvironmentFor(source, '/jobs/experimental', '/data', false, true, true);
  assert.equal(experimentalAudition.PULSO_AI_RESPONSE_CACHE_DIR, undefined);
});

test('ordinary AI-sovereign requests use first-pass phrase planning by default', () => {
  assert.deepEqual(defaultEditorialSettings({}, false, 'ai_sovereign'),
    { firstPass: true, phraseArchitecture: true });
  assert.deepEqual(defaultEditorialSettings({}, true, 'ai_sovereign'),
    { firstPass: false, phraseArchitecture: false });
  assert.deepEqual(defaultEditorialSettings({}, false, 'standard'),
    { firstPass: false, phraseArchitecture: false });
  assert.deepEqual(defaultEditorialSettings({ review_from_job: 'source' }, false, 'ai_sovereign'),
    { firstPass: false, phraseArchitecture: true });
});

test('first-pass audition never starts an automatic editorial recovery after a failure', () => {
  const job = { state: 'failed', request: { ai_sovereign: true, proof_mode: false },
    checkpoint: { file: 'partial-01.mid' }, error: 'temporary score failure' };
  assert.equal(canAutomaticallyRecover(job, true), true);
  job.request.first_pass_audition = true;
  assert.equal(canAutomaticallyRecover(job, true), false);
});

test('controlled review audition is local, explicit and cannot trigger automatic recovery', () => {
  const source = { PULSO_AI_REVIEW_AUDITION: '1' };
  const normal = workerEnvironmentFor(source, '/jobs/normal', '/data', false, false);
  const review = workerEnvironmentFor(source, '/jobs/review', '/data', false, false, false, true);
  assert.equal(normal.PULSO_AI_REVIEW_AUDITION, undefined);
  assert.equal(review.PULSO_AI_REVIEW_AUDITION, '1');
  const job = { state: 'failed', request: { ai_sovereign: true,
    review_from_job: 'source', proof_mode: false },
    checkpoint: { file: 'partial-05.mid' }, error: 'temporary score failure' };
  assert.equal(canAutomaticallyRecover(job, true), false);
});

test('phrase architecture experiment is isolated to its requested local job', () => {
  const source = { PULSO_AI_PHRASE_ARCHITECTURE_EXPERIMENT: '1' };
  const normal = workerEnvironmentFor(source, '/jobs/normal', '/data', false, false);
  const phrase = workerEnvironmentFor(source, '/jobs/phrases', '/data', false, false,
    true, false, true);
  assert.equal(normal.PULSO_AI_PHRASE_ARCHITECTURE_EXPERIMENT, undefined);
  assert.equal(phrase.PULSO_AI_PHRASE_ARCHITECTURE_EXPERIMENT, '1');
});

test('an explicitly experimental local worker is visibly outside production parity', () => {
  const env = workerEnvironmentFor({ PULSO_AI_FAST_MODE: '1',
    PULSO_AI_MELODIC_EXPERIMENT: '1',
    PULSO_AI_GLOBAL_NARRATIVE_EXPERIMENT: '1',
    PULSO_AI_NARRATIVE_FULL_SCENE: '1',
    PULSO_AI_RHYTHMIC_ARC_EXPERIMENT: '1',
    PULSO_AI_NARRATIVE_FINALE_EXPERIMENT: '1',
    PULSO_AI_NARRATIVE_FOCUS: 'example' }, '/jobs/two', '/data', true, true);
  assert.equal(env.PULSO_AI_FAST_MODE, '1');
  assert.equal(env.PULSO_AI_MELODIC_EXPERIMENT, '1');
  assert.equal(env.PULSO_AI_GLOBAL_NARRATIVE_EXPERIMENT, '1');
  assert.equal(env.PULSO_AI_GLOBAL_NARRATIVE_EDITOR, '1');
  assert.equal(env.PULSO_AI_NARRATIVE_FULL_SCENE, '1');
  assert.equal(env.PULSO_AI_RHYTHMIC_ARC_EXPERIMENT, '1');
  assert.equal(env.PULSO_AI_NARRATIVE_FINALE_EXPERIMENT, '1');
  assert.equal(env.PULSO_AI_NARRATIVE_FOCUS, 'example');
  assert.equal(env.PULSO_AUTO_RECOVERY_MAX_SECONDS, '600');
  assert.equal(env.PULSO_AUTO_RECOVERY_MAX_USD, '20');
});
