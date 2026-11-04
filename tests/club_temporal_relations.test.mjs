import {readFileSync} from 'node:fs';
import assert from 'node:assert/strict';
import test from 'node:test';
const read = p => readFileSync(new URL('../' + p, import.meta.url), 'utf8');
test('temporal Club guidance reaches planning and bounded writing without MIDI edits', () => {
  const temporal = read('src/plugin/AiClubTemporalRelations.h');
  assert.ok(read('src/plugin/AiClubDramaturgy.h').includes('+ temporal::brief()'));
  assert.ok(read('src/plugin/AiClubProductionPlan.h').includes('if (nucleus::enabled()) out << temporal::context(root, begin, end, beatsPerBar)'));
  assert.ok(temporal.includes('stop <= begin && stop > previousEnd'));
  assert.ok(temporal.includes('start >= end && start < nextStart'));
  assert.ok(temporal.includes('actual accepted notes'));
  assert.ok(temporal.includes('not permission to reveal it now'));
  assert.doesNotMatch(temporal, /addNote|setPitch|setDuration|MidiMessage/);
});

