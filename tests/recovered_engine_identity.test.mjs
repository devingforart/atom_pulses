import { readFileSync } from 'node:fs';
import { createHash } from 'node:crypto';
import assert from 'node:assert/strict';
import test from 'node:test';

const read = file => readFileSync(new URL('../' + file, import.meta.url), 'utf8').replace(/\r\n/g, '\n');
const blob = text => createHash('sha1').update('blob ' + Buffer.byteLength(text) + '\0').update(text).digest('hex');
test('recovered engine pins the reviewed composer and preserves other historical components', () => {
  for (const [file, hash] of [
    ['src/plugin/AiComposer.cpp', 'c631a01d8ae92b8abdd5120472b8f2e28d663e6f'],
    ['src/plugin/AiClubProductionPlan.h', '8b8e7c362bc5e1dd444a577b8e83c739dfc5b3ea'],
    ['web/shared/composer-profile.json', '6068fa539394d528aad685c39f50d93c96028e9a'],
    ['tools/pulso_cloud_worker.cpp', '26e76584b91d80af81bf64ed1f9fc7c92ee66ec9'],
  ]) assert.equal(blob(read(file)), hash, file);
  const nucleus = read('src/plugin/AiClubNucleus.h')
    .replace('#include "AiClubDramaturgy.h"\n', '')
    .replaceAll(' + dramaturgy::brief()', '')
    .replace(/inline juce::String localClubPreferences\(\) \{[\s\S]*?\n\}\n\n/, '')
    .replaceAll(' + localClubPreferences()', '');
  assert.equal(blob(nucleus), '4d0b5f93e2ea05feba22ec17269b0e382cb79b91', 'Accepted Club baseline includes focused nucleus recovery');
});

test('controlled review coordinates final owners and rejects silent targets before API rewriting', () => {
  const composer = read('src/plugin/AiComposer.cpp');
  assert.match(composer, /behavior == CompositionBehavior::Club && completeScoreResume &&\s*juce::SystemStats::getEnvironmentVariable\("PULSO_AI_REVIEW_AUDITION"/);
  assert.match(composer, /if \(entry < absoluteEnd && release > absoluteStart\)\s*revisionOwners.push_back\(owner\)/);
  assert.match(composer, /skipped target outside its authored activity span; no paid rewrite requested/);
  assert.match(composer, /harmonicSafe && narrativeImproved && releasePreserved &&/);
});

test('Club dramaturgy reaches editorial, planning and writing without MIDI operations', () => {
  assert.equal(read('src/plugin/AiClubNucleus.h').match(/\+ dramaturgy::brief\(\)/g)?.length, 3);
  const brief = read('src/plugin/AiClubDramaturgy.h');
  assert.match(brief, /scale membership alone/);
  assert.match(brief, /not only a final tonic/);
  assert.doesNotMatch(brief, /Pattern|MidiMessage|addNote|setPitch/);
});
