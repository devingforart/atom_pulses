import {readFileSync} from 'node:fs';
import test from 'node:test';
import assert from 'node:assert/strict';
import {workerEnvironmentFor} from '../web/local-studio/composer-environment.mjs';
test('local discourse-first is opt-in and does not alter default environment', () => {
  assert.equal(workerEnvironmentFor({},'C:/tmp/job','C:/tmp/data').PULSO_AI_CLUB_ENSEMBLE_SCENES,undefined);
  assert.equal(workerEnvironmentFor({PULSO_AI_CLUB_ENSEMBLE_SCENES:'1'},'C:/tmp/job','C:/tmp/data').PULSO_AI_CLUB_ENSEMBLE_SCENES,'1');
  assert.equal(workerEnvironmentFor({},'C:/tmp/job','C:/tmp/data').PULSO_AI_CLUB_MELODY_FIRST,undefined);
  assert.equal(workerEnvironmentFor({PULSO_AI_CLUB_MELODY_FIRST:'1'},'C:/tmp/job','C:/tmp/data').PULSO_AI_CLUB_MELODY_FIRST,'1');
});
test('whole foreground pass precedes support and uses persisted checkpoint units', () => {
  const source=readFileSync(new URL('../src/plugin/AiComposer.cpp',import.meta.url),'utf8');
  const start=source.indexOf('for (std::size_t discoursePass = 0;');
  assert.ok(start>0);
  assert.ok(source.indexOf('for (std::size_t sectionIndex = 0;',start)>start);
  assert.ok(source.includes('windowCount *= discoursePasses'));
  assert.ok(source.includes('windowOrdinal++ < restoredWindows'));
  assert.ok(source.includes('club::production::discourseFirst(result)'));
});
