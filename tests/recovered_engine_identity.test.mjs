import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';
import test from 'node:test';

const read = file => readFileSync(new URL('../' + file, import.meta.url), 'utf8').replace(/\r\n/g, '\n');
const blob = text => createHash('sha1').update('blob ' + Buffer.byteLength(text) + '\0').update(text).digest('hex');
test('historical composing engine and profile stay exact outside the two optional Club briefs', () => {
  for (const [file, hash] of [
    ['src/plugin/AiComposer.cpp', '93d239e7d0a53aa0d9f2de8948d7ff4779f241a9'],
    ['src/plugin/AiClubProductionPlan.h', '9c7c8cfab1b01b628235e220d6ec140af3f4fd66'],
    ['web/shared/composer-profile.json', '6068fa539394d528aad685c39f50d93c96028e9a'],
    ['tools/pulso_cloud_worker.cpp', '26e76584b91d80af81bf64ed1f9fc7c92ee66ec9'],
  ]) assert.equal(blob(read(file)), hash, file);
  const nucleus = read('src/plugin/AiClubNucleus.h')
    .replace(/inline juce::String localClubPreferences\(\) \{[\s\S]*?\n\}\n\n/, '')
    .replaceAll(' + localClubPreferences()', '');
  assert.equal(blob(nucleus), 'c2fbd6fe8c7bf392971c44030e86d4da82c0ef15', 'Only the two opt-in prompt preferences may differ');
});
