import test from 'node:test';
import assert from 'node:assert/strict';
import path from 'node:path';
import { composerProfile, workerEnvironmentFor } from '../web/local-studio/composer-environment.mjs';

test('local production worker uses exactly the shared Cloud composer profile', () => {
  const source = { OPENAI_API_KEY: 'test-only', PATH: '/bin',
    PULSO_AI_FAST_MODE: '1', PULSO_AI_EXPERIMENT_MODEL: 'gpt-6-luna' };
  const env = workerEnvironmentFor(source, '/jobs/one', '/data', false, false);
  for (const [key, value] of Object.entries(composerProfile.workerEnvironment))
    assert.equal(env[key], value, key);
  assert.equal(env.OPENAI_API_KEY, 'test-only');
  assert.equal(env.PULSO_TRACE_PATH, path.join('/jobs/one', 'api-events.jsonl'));
  assert.equal(env.PULSO_AI_FAST_MODE, undefined);
  assert.equal(env.PULSO_AI_EXPERIMENT_MODEL, undefined);
  assert.equal(env.PULSO_AI_RESPONSE_CACHE_DIR, undefined);
});

test('an explicitly experimental local worker is visibly outside production parity', () => {
  const env = workerEnvironmentFor({ PULSO_AI_FAST_MODE: '1' }, '/jobs/two', '/data', true, true);
  assert.equal(env.PULSO_AI_FAST_MODE, '1');
  assert.equal(env.PULSO_AUTO_RECOVERY_MAX_SECONDS, '600');
  assert.equal(env.PULSO_AUTO_RECOVERY_MAX_USD, '20');
});
