import assert from 'node:assert/strict';
import { test } from 'node:test';
import { defaultEditorialSettings, workerEnvironmentFor } from '../web/local-studio/composer-environment.mjs';

test('local Club candidate uses first-pass listening unless explicitly overridden', () => {
  assert.equal(defaultEditorialSettings({ behavior: 'club' }, false, 'ai_sovereign').firstPass, true);
  assert.equal(defaultEditorialSettings({ behavior: 'club', first_pass_audition: false }, false, 'ai_sovereign').firstPass, false);
  assert.equal(defaultEditorialSettings({ behavior: 'club' }, true, 'ai_sovereign').firstPass, false);
});

test('local Club joint-groove contract is passed to new workers by default', () => {
  const env = workerEnvironmentFor({}, 'C:/candidate', 'C:/candidate-data', false, false);
  assert.equal(env.PULSO_AI_CLUB_JOINT_GROOVE_EXPERIMENT, '1');
  assert.equal(env.PULSO_AI_JOINT_ONE_GROUP, '1');
});
