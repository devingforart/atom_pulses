import { readFileSync } from 'node:fs';
import assert from 'node:assert/strict';
import test from 'node:test';

const read = file => readFileSync(new URL('../' + file, import.meta.url), 'utf8');
test('developed arrangement travels through shared Club planning and writing', () => {
  const brief = read('src/plugin/AiClubDramaturgy.h');
  const nucleus = read('src/plugin/AiClubNucleus.h');
  assert.equal(nucleus.match(/\+ dramaturgy::brief\(\)/g)?.length, 3);
  for (const text of ['DEVELOPED CLUB ARRANGEMENT', 'interaction/harmonic_job',
    'state material/evolution', 'musical_arcs', 'actual jointly written notes',
    'phrase_flexible', 'not a compulsory', 'neutral piano']) assert.ok(brief.includes(text), text);
  assert.doesNotMatch(brief, /MidiMessage|addNote|setPitch|setDuration|std::vector/);
});

test('secondary statements use existing arc owners without imposing note quotas', () => {
  const brief = read('src/plugin/AiClubDramaturgy.h');
  const plan = read('src/plugin/AiClubProductionPlan.h');
  assert.ok(brief.includes('SECONDARY THEMATIC AGENCY'));
  assert.ok(brief.includes('foreground_id may change locally'));
  assert.ok(brief.includes('solo or deliberately'));
  assert.ok(plan.includes('Existing cast owner of THIS statement'));
  assert.ok(plan.includes('How this owner\'s actual phrase develops'));
});

test('listener-preferred secondary-agency baseline excludes the discarded third trial', () => {
  const brief = read('src/plugin/AiClubDramaturgy.h');
  const plan = read('src/plugin/AiClubProductionPlan.h');
  assert.ok(!brief.includes('INDEPENDENT DEVELOPMENT RATES'));
  assert.ok(!brief.includes('REALIZED ARRIVALS'));
  assert.ok(!plan.includes('AI SCORE SELF-CHECK'));
});
