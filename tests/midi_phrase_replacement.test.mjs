import test from 'node:test';
import assert from 'node:assert/strict';
import { parseMidi, singleTrackMidi } from '../web/shared/midi.mjs';
import { replaceMidiPhrases } from '../tools/lib/midi-phrase-replacement.mjs';

const name='PULSO - Todas las notas';
const note=(startBeat,durationBeats,pitch)=>({startBeat,durationBeats,pitch,velocity:80,channel:0});
const source=()=>Buffer.from(singleTrackMidi({division:480,beatsPerBar:4,lengthBeats:24,
  tempos:[{tick:0,microseconds:500000}]},[{notes:[note(0,2,67),note(4,1,65),note(8,1,62),note(16,2,69)]}]));

test('serializes AI replacement exactly, with a different note count and unchanged outside notes',()=>{
  const original=source(),replacement=[note(4,2,65),note(6,4,64),note(10,3,62)];
  const bytes=replaceMidiPhrases(original,[{name,notes:replacement}],4,16);
  const result=parseMidi(bytes);
  assert.deepEqual(result.tracks[0].notes,[note(0,2,67),...replacement,note(16,2,69)]);
  assert.equal(result.lengthBeats,24);
  assert.equal(result.tempos.at(-1).microseconds,500000);
  assert.deepEqual(original,source());
});

test('rejects out-of-scope notes and ambiguous owners rather than clipping or guessing',()=>{
  assert.throws(()=>replaceMidiPhrases(source(),[{name,notes:[note(15,2,62)]}],4,16),/Unrepresentable/);
  assert.throws(()=>replaceMidiPhrases(source(),[{name:'missing',notes:[]}],4,16),/Ambiguous/);
  assert.throws(()=>replaceMidiPhrases(source(),[{name,notes:[]},{name,notes:[]}],4,16),/Ambiguous/);
});

test('preserves untouched bytes, same-pitch reattack integrity, and immutable boundary notes',()=>{
  const original=source();
  assert.deepEqual(replaceMidiPhrases(original,[],4,16),original);
  const result=parseMidi(replaceMidiPhrases(original,[{name,notes:[note(4,4,62),note(8,4,62)]}],4,16));
  assert.deepEqual(result.tracks[0].notes,[note(0,2,67),note(4,4,62),note(8,4,62),note(16,2,69)]);
  assert.throws(()=>replaceMidiPhrases(original,[{name,notes:[]}],1,16),/crosses/);
});

test('a replacement releasing exactly at score end precedes end-of-track and never hangs',()=>{
  const expected=[note(0,2,67),note(4,20,62)];
  const result=parseMidi(replaceMidiPhrases(source(),[{name,notes:[note(4,20,62)]}],4,24));
  assert.deepEqual(result.tracks[0].notes,expected);
  assert.equal(result.lengthBeats,24);
});

test('two independently authored disjoint regions compose without changing earlier notes',()=>{
  const first=replaceMidiPhrases(source(),[{name,notes:[note(4,3,64)]}],4,16);
  const second=replaceMidiPhrases(first,[{name,notes:[note(16,6,62)]}],16,24);
  assert.deepEqual(parseMidi(second).tracks[0].notes,[note(0,2,67),note(4,3,64),note(16,6,62)]);
});
