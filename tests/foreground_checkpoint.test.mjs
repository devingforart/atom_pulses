import test from 'node:test';
import assert from 'node:assert/strict';
import { foregroundCheckpoint } from '../web/local-studio/foreground-checkpoint.mjs';

test('foreground comparison retains exact authored notes, full cast and plan without accompaniment', () => {
  const source = { schema_version: 2, prewritten_dialogue: true, completed_units: 7,
    cast_ids: ['lead', 'bed'], blueprint_json: '{}', phrase_architecture_json: '{}',
    sections: [{start_bar: 0, bars: 8}], notes: [] };
  const plan = { cast: [{id: 'lead', name: 'Lead'}, {id: 'bed', name: 'Bed'}] };
  const note = { startBeat: 3.25, durationBeats: 1.5, pitch: 69, velocity: 88 };
  const before = JSON.stringify(source);
  const result = foregroundCheckpoint(source, plan, {tracks: [{name: 'PULSO Lead', notes: [note]}]});
  assert.equal(result.completed_units, 0);
  assert.deepEqual(result.cast_ids, ['lead', 'bed']);
  assert.deepEqual(result.notes, [{instrument_id:'lead',start_beat:3.25,duration_beats:1.5,pitch:69,velocity:88}]);
  assert.equal(JSON.stringify(source), before);
});

test('foreground comparison refuses wrong cast and hanging notes rather than repairing them', () => {
  const source = {schema_version:2,prewritten_dialogue:true,cast_ids:['lead']};
  const plan = {cast:[{id:'lead',name:'Lead'}]};
  assert.throws(() => foregroundCheckpoint({...source,cast_ids:['other']},plan,{tracks:[]}));
  assert.throws(() => foregroundCheckpoint(source,plan,{tracks:[{name:'PULSO Lead',notes:[
    {startBeat:0,durationBeats:1,pitch:69,velocity:88,unterminated:true}]}]}));
});
