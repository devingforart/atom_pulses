// Isolated listening experiment: the AI supplies timing; no algorithm composes notes.
// Original MIDI events, including controllers and metadata, remain intact otherwise.
import fs from 'node:fs';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { parseMidi } from '../web/shared/midi.mjs';

const sourceId = process.argv[2];
if (!/^[0-9a-f-]{36}$/.test(sourceId || '')) throw Error('Provide a local source job UUID');
if (!process.env.OPENAI_API_KEY) throw Error('OPENAI_API_KEY missing');
const root = path.resolve('build/local-studio-data/jobs');
const source = path.join(root, sourceId);
const score = parseMidi(fs.readFileSync(path.join(source, 'full-song.mid')));
const voices = ['PULSO First violin protagonist', 'PULSO Second violin motif answerer'];
const selected = score.tracks.flatMap((t, ti) => t.notes.flatMap((n, ni) =>
  voices.includes(t.name) && n.startBeat >= 4 && n.startBeat < 16
    ? [{ id: `t${ti}n${ni}`, track: ti, index: ni, ...n }] : []));
if (!selected.length) throw Error('No editable notes');
const id = randomUUID(), destination = path.join(root, id);
fs.mkdirSync(destination);
const write = (name, value) => fs.writeFileSync(path.join(destination, name), JSON.stringify(value, null, 2));
write('local-origin.json', { origin: 'auto' });
const context = score.tracks.filter(t => t.notes.length).map(t => ({ name: t.name,
  notes: t.notes.filter(n => n.startBeat < 32).map(n => [n.startBeat, n.durationBeats, n.pitch, n.velocity]) }));
const schema = { type: 'object', additionalProperties: false,
  required: ['explanation', 'timing'], properties: { explanation: { type: 'string' },
    timing: { type: 'object', additionalProperties: false, required: selected.map(n => n.id),
      properties: Object.fromEntries(selected.map(n => [n.id, {
        type: 'object', additionalProperties: false, required: ['start', 'duration'],
        properties: { start: { type: 'number', minimum: 4, maximum: 15.75, multipleOf: 0.25 },
          duration: { type: 'number', minimum: 0.25, maximum: 12, multipleOf: 0.25 } },
      }])) } } };
const instructions = `You are editing the articulation of an existing musical dialogue, not recomposing it.
The listener likes its complexity in later bars, but bars 2-4 sound hesitant, stumbled and disconnected through one piano.
Only edit the supplied note IDs: onset and sounding duration. Preserve EVERY pitch, velocity, note count and pitch order.
All other voices and bars are immutable. Coordinates are zero-based quarter beats, 4 beats/bar, 120 BPM.
Make the first violin a comprehensible connected sentence and its partner a related, articulate answer.
Inspect the Re4 reattack across beats 7.75/8.5 and the interleaved second violin, not just fast notes in isolation.
Do not mechanically straighten syncopation, slow everything, duplicate notes, or fill every silence.
Retain character and useful rhythmic identity; give musical purpose to interruptions and reattacks.
Every edited note must start in [4,16) and end <=16. Do not overlap successive notes of the SAME voice.
The notes before beat 4 and after beat 16 are context, not editable. No changed pitches or outside-bar edits.
Return the selected timings and a short Spanish explanation; you have symbolic notes, not rendered audio.`;
const headers = { Authorization: `Bearer ${process.env.OPENAI_API_KEY}`, 'Content-Type': 'application/json' };
async function request(url, options = {}) {
  const r = await fetch(url, { ...options, headers, signal: AbortSignal.timeout(60000) });
  if (!r.ok) throw Error(`AI HTTP ${r.status}; no automatic retry`);
  return r.json();
}
let response = await request('https://api.openai.com/v1/responses', { method: 'POST', body: JSON.stringify({
  model: 'gpt-5.6-terra', reasoning: { effort: 'medium' }, background: true, max_output_tokens: 3500,
  instructions, input: JSON.stringify({ editable: selected, context }),
  text: { format: { type: 'json_schema', name: 'timing_only_edit', strict: true, schema } },
}) });
write('timing-edit-request.json', { sourceId, responseId: response.id, selected, instructions });
console.log(JSON.stringify({ id, responseId: response.id, editableNotes: selected.length }));
const started = Date.now();
while (['queued', 'in_progress'].includes(response.status)) {
  if (Date.now() - started > 600000) throw Error('Remote response pending; recover saved response ID, do not resubmit');
  await delay(2500);
  response = await request(`https://api.openai.com/v1/responses/${response.id}`);
}
write('timing-edit-response.json', response);
if (response.status !== 'completed') throw Error(`AI response ${response.status}`);
const revision = JSON.parse(response.output.flatMap(o => o.content || []).filter(c => c.type === 'output_text').map(c => c.text).join(''));
const edits = selected.map(n => ({ ...n, replacement: revision.timing[n.id] }));
for (const n of edits) {
  const e = n.replacement;
  if (!e || !Number.isFinite(e.start) || !Number.isFinite(e.duration) ||
      e.start < 4 || e.start >= 16 || e.duration <= 0 || e.start + e.duration > 16 ||
      !Number.isInteger(e.start * 4) || !Number.isInteger(e.duration * 4)) throw Error('Invalid timing; no automatic correction');
}
for (const ti of new Set(edits.map(n => n.track))) {
  const notes = score.tracks[ti].notes.map((n, ni) => {
    const e = edits.find(e => e.track === ti && e.index === ni)?.replacement;
    return e ? { ...n, startBeat: e.start, durationBeats: e.duration } : n;
  });
  for (let i = 1; i < notes.length; i++)
    if (notes[i].startBeat < notes[i-1].startBeat + notes[i-1].durationBeats - 1e-8)
      throw Error('AI proposed overlap or reordered voice; no automatic correction');
}

// Decode/re-encode delta times only; retain original event bytes and untouched tracks.
const vlq = value => { const b = [value & 127]; while ((value >>>= 7)) b.unshift((value & 127) | 128); return b; };
function editMidi(bytes, replacements) {
  const b = Buffer.from(bytes); let offset = 8 + b.readUInt32BE(4);
  const chunks = [b.subarray(0, offset)];
  for (let ti = 0; ti < b.readUInt16BE(10); ti++) {
    const begin = offset, length = b.readUInt32BE(offset + 4), end = offset + 8 + length;
    offset += 8; let tick = 0, running = 0; const events = [], active = new Map();
    const variable = () => { let v = 0, c; do { c = b[offset++]; v = (v << 7) | (c & 127); } while(c & 128); return v; };
    while(offset < end) {
      tick += variable(); let status = b[offset++];
      if(status < 128) { offset--; status = running; } else if(status < 240) running = status;
      let data;
      if(status === 255) { const kind = b[offset++], len = variable(); data = [255, kind, ...vlq(len), ...b.subarray(offset, offset + len)]; offset += len; }
      else if(status === 240 || status === 247) { const len = variable(); data = [status, ...vlq(len), ...b.subarray(offset, offset + len)]; offset += len; }
      else { const len = (status & 240) === 192 || (status & 240) === 208 ? 1 : 2; data = [status, ...b.subarray(offset, offset + len)]; offset += len; }
      const event = { tick, data, order: events.length }; events.push(event);
      const type = status & 240, key = `${status & 15}:${data[1]}`;
      if(type === 144 && data[2] > 0) { const q = active.get(key) || []; q.push(event); active.set(key,q); }
      else if(type === 128 || (type === 144 && data[2] === 0)) {
        const on = active.get(key)?.shift();
        if(on) {
          const e = replacements.find(n => n.track === ti && n.pitch === data[1] && n.channel === (status & 15) &&
            Math.round(n.startBeat * score.division) === on.tick);
          if(e) { on.tick = Math.round(e.replacement.start * score.division); event.tick = Math.round((e.replacement.start + e.replacement.duration) * score.division); }
        }
      }
    }
    if(!replacements.some(n => n.track === ti)) { chunks.push(b.subarray(begin,end)); continue; }
    events.sort((a,c) => a.tick-c.tick || (((a.data[0]&240)===128 || ((a.data[0]&240)===144 && a.data[2]===0)) ? 0 : 1) -
      (((c.data[0]&240)===128 || ((c.data[0]&240)===144 && c.data[2]===0)) ? 0 : 1) || a.order-c.order);
    const payload = []; let previous = 0;
    for(const e of events) { payload.push(...vlq(e.tick-previous), ...e.data); previous=e.tick; }
    const header=Buffer.alloc(8); header.write('MTrk'); header.writeUInt32BE(payload.length,4);
    chunks.push(header,Buffer.from(payload));
  }
  return Buffer.concat(chunks);
}
const revised = editMidi(fs.readFileSync(path.join(source,'full-song.mid')),edits);
const checked = parseMidi(revised);
for(let ti=0;ti<score.tracks.length;ti++) {
  if(checked.tracks[ti].notes.length !== score.tracks[ti].notes.length) throw Error('Count changed');
  score.tracks[ti].notes.forEach((n,ni) => {
    const e=edits.find(e=>e.track===ti && e.index===ni)?.replacement;
    const expected={...n,...(e?{startBeat:e.start,durationBeats:e.duration}:{})};
    if(JSON.stringify(expected)!==JSON.stringify(checked.tracks[ti].notes[ni])) throw Error('Unexpected MIDI difference');
  });
}
const manifest=JSON.parse(fs.readFileSync(path.join(source,'manifest.json'),'utf8'));
for(const name of ['composition-plan.json','phrase-architecture.json']) fs.copyFileSync(path.join(source,name),path.join(destination,name));
for(let i=0;i<manifest.tracks.length;i++) {
  const track=manifest.tracks[i]; const raw=fs.readFileSync(path.join(source,track.filename));
  const single=parseMidi(raw); const fullIndex=score.tracks.findIndex(t=>t.name===`PULSO ${track.name}`);
  const singleIndex=single.tracks.findIndex(t=>t.notes.length);
  const mapped=edits.filter(n=>n.track===fullIndex).map(n=>({...n,track:singleIndex}));
  fs.writeFileSync(path.join(destination,track.filename),mapped.length?editMidi(raw,mapped):raw);
}
fs.writeFileSync(path.join(destination,'full-song.mid'),revised);
write('timing-edit-audit.json',{sourceId, explanation:revision.explanation, changes:edits.filter(n=>n.startBeat!==n.replacement.start || n.durationBeats!==n.replacement.duration),
  verified:{pitches:true,velocities:true,noteCount:true,outsideScopeUnchanged:true,controllersAndOtherTracksPreserved:true}, usage:response.usage});
const job=JSON.parse(fs.readFileSync(path.join(source,'job.json'),'utf8'));
write('job.json',{...job,created_at:new Date().toISOString(),timing_edit_from_job:sourceId});
write('manifest.json',{...manifest,title:'The Fourth That Returns — B · articulación 2–4',thematic_rehearsal:false,timing_edit_from_job:sourceId});
console.log(JSON.stringify({id,changedNotes:edits.filter(n=>n.startBeat!==n.replacement.start || n.durationBeats!==n.replacement.duration).length,explanation:revision.explanation,url:`http://localhost:4198/?job=${id}`}));
