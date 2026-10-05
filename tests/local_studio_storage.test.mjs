import assert from 'node:assert/strict';
import { mkdtempSync, mkdirSync, rmSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import test from 'node:test';
import { dataRoot, jobDirectory, jobsRoot, listJobIds } from '../web/local-studio/storage.mjs';

test('new local sessions stay inside the ignored project build directory', () => {
  if (process.env.PULSO_LOCAL_DATA_DIR) return;
  assert.equal(dataRoot, path.resolve('build', 'local-studio-data'));
  assert.equal(jobsRoot, path.join(dataRoot, 'jobs'));
});

test('the current store wins while older jobs remain readable', () => {
  const temporary = mkdtempSync(path.join(os.tmpdir(), 'pulso-storage-test-'));
  const current = path.join(temporary, 'current');
  const legacy = path.join(temporary, 'legacy');
  const oldId = '11111111-1111-4111-8111-111111111111';
  const sharedId = '22222222-2222-4222-8222-222222222222';
  try {
    mkdirSync(path.join(legacy, oldId), { recursive: true });
    mkdirSync(path.join(legacy, sharedId), { recursive: true });
    mkdirSync(path.join(current, sharedId), { recursive: true });
    assert.deepEqual(new Set(listJobIds([current, legacy])), new Set([oldId, sharedId]));
    assert.equal(jobDirectory(oldId, [current, legacy]), path.join(legacy, oldId));
    assert.equal(jobDirectory(sharedId, [current, legacy]), path.join(current, sharedId));
    assert.equal(jobDirectory('not-a-uuid', [current, legacy]), null);
  } finally {
    rmSync(temporary, { recursive: true, force: true });
  }
});
