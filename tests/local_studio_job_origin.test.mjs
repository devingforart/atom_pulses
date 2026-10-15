import test from 'node:test';
import assert from 'node:assert/strict';
import { AUTO_ORIGIN, PERSONAL_ORIGIN, originForJob, originForRoute } from '../web/local-studio/job-origin.mjs';

test('personal UI submissions and automated local experiments have distinct origins', () => {
  assert.equal(originForRoute('/api/jobs'), PERSONAL_ORIGIN);
  assert.equal(originForRoute('/api/auto/jobs'), AUTO_ORIGIN);
});

test('legacy jobs and unknown metadata default to Personal', () => {
  assert.equal(originForJob(null), PERSONAL_ORIGIN);
  assert.equal(originForJob({ origin: 'unexpected' }), PERSONAL_ORIGIN);
  assert.equal(originForJob({ origin: AUTO_ORIGIN }), AUTO_ORIGIN);
});
