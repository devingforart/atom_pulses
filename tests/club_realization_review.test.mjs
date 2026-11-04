import {readFileSync} from 'node:fs';
import assert from 'node:assert/strict';
import test from 'node:test';
const source = readFileSync(new URL('../src/plugin/AiComposer.cpp', import.meta.url), 'utf8');
test('Club reviewer sees non-melodic owners, held notes, actual harmony and promised arcs', () => {
  assert.match(source, /behavior == CompositionBehavior::Club \|\|\s*isVoiceInFamily/);
  assert.match(source, /note\.endBeat\(\) <= start \+ \.001/);
  assert.ok(source.includes('PLANNED MUSICAL ARCS (absolute zero-based bars)'));
  assert.ok(source.includes('CLUB PLAN-TO-SCORE REVIEW'));
  assert.ok(source.includes('target their original onset window'));
  assert.ok(source.includes('FOCUSED CLUB REALIZATION REPAIR'));
  assert.match(source, /A complete "\s*"unchanged phrase is valid/);
});
test('first pass still bypasses the optional review rather than silently adding cost', () => {
  assert.ok(source.indexOf('First-pass audition: musical revisions bypassed') < source.indexOf('CLUB PLAN-TO-SCORE REVIEW'));
  assert.ok(source.includes('harmonicSafe && narrativeImproved && releasePreserved'));
});
