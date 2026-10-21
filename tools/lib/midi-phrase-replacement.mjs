// Representation-only edit: serialize an explicit AI-authored phrase without
// inventing, quantizing, joining, deleting or correcting any of its notes.
import { parseMidi } from '../../web/shared/midi.mjs';

const vlq = value => {
  if (!Number.isInteger(value) || value < 0 || value > 0x0fffffff) throw Error('Invalid delta time');
  const bytes = [value & 127];
  while ((value >>>= 7)) bytes.unshift((value & 127) | 128);
  return bytes;
};
const isOff = e => (e.data[0] & 240) === 128 || ((e.data[0] & 240) === 144 && e.data[2] === 0);
const isOn = e => (e.data[0] & 240) === 144 && e.data[2] > 0;

export function replaceMidiPhrases(input, replacements, startBeat, endBeat) {
  const b = Buffer.from(input), score = parseMidi(b);
  if (!(startBeat >= 0 && endBeat > startBeat)) throw Error('Invalid phrase interval');
  const owners = new Map();
  for (const part of replacements) {
    const indices = score.tracks.flatMap((t,i) => t.name === part.name ? [i] : []);
    if (indices.length !== 1 || owners.has(indices[0])) throw Error('Ambiguous phrase owner');
    for (const note of part.notes) {
      if (!Number.isFinite(note.startBeat) || !Number.isFinite(note.durationBeats) ||
          note.startBeat < startBeat || note.startBeat >= endBeat || note.durationBeats <= 0 ||
          note.startBeat + note.durationBeats > endBeat || !Number.isInteger(note.pitch) ||
          note.pitch < 0 || note.pitch > 127 || !Number.isInteger(note.velocity) || note.velocity < 1 || note.velocity > 127 ||
          !Number.isInteger(note.channel) || note.channel < 0 || note.channel > 15 ||
          !Number.isInteger(note.startBeat * score.division) || !Number.isInteger(note.durationBeats * score.division))
        throw Error('Unrepresentable AI note; no automatic correction');
    }
    const original = score.tracks[indices[0]].notes;
    if (original.some(n => n.unterminated || (n.startBeat < startBeat && n.startBeat+n.durationBeats > startBeat)))
      throw Error('Phrase boundary crosses an immutable note');
    owners.set(indices[0],part);
  }
  let offset = 8+b.readUInt32BE(4);
  const chunks = [b.subarray(0,offset)];
  for (let ti=0;ti<score.tracks.length;ti++) {
    const begin=offset, end=offset+8+b.readUInt32BE(offset+4), part=owners.get(ti);
    if (!part) { chunks.push(b.subarray(begin,end)); offset=end; continue; }
    offset+=8;
    let tick=0,running=0;
    const events=[],active=new Map();
    const variable=()=>{let v=0,c;do{c=b[offset++];v=(v<<7)|(c&127);}while(c&128);return v;};
    while(offset<end) {
      tick+=variable();let status=b[offset++];
      if(status<128){offset--;status=running;}else if(status<240)running=status;
      let data;
      if(status===255){const kind=b[offset++],len=variable();data=[255,kind,...vlq(len),...b.subarray(offset,offset+len)];offset+=len;}
      else if(status===240||status===247){const len=variable();data=[status,...vlq(len),...b.subarray(offset,offset+len)];offset+=len;}
      else{const len=[192,208].includes(status&240)?1:2;data=[status,...b.subarray(offset,offset+len)];offset+=len;}
      const event={tick,data,order:events.length,removed:false};events.push(event);
      const key=`${status&15}:${data[1]}`;
      if(isOn(event)) {
        event.removed=tick>=startBeat*score.division&&tick<endBeat*score.division;
        const q=active.get(key)||[];q.push(event);active.set(key,q);
      } else if(isOff(event)) {
        const on=active.get(key)?.shift();
        if(!on)throw Error('Orphan MIDI release');
        event.removed=on.removed;
      }
    }
    if([...active.values()].some(q=>q.length))throw Error('Unterminated MIDI note');
    for(const n of part.notes) {
      events.push({tick:n.startBeat*score.division,data:[144|n.channel,n.pitch,n.velocity],order:events.length});
      events.push({tick:(n.startBeat+n.durationBeats)*score.division,data:[128|n.channel,n.pitch,0],order:events.length});
    }
    // Preserve original ordering at every timestamp except simultaneous
    // note-off/on of the same pitch, whose release must precede its reattack.
    const isEnd=e=>e.data[0]===255&&e.data[1]===47;
    const kept=events.filter(e=>!e.removed).sort((a,c)=>a.tick-c.tick||Number(isEnd(a))-Number(isEnd(c))||a.order-c.order);
    for(let i=0;i<kept.length;) {
      let j=i+1;while(j<kept.length&&kept[j].tick===kept[i].tick)j++;
      for(let k=i;k<j;k++)if(isOn(kept[k])) {
        const release=kept.findIndex((e,x)=>x>k&&x<j&&isOff(e)&&
          (e.data[0]&15)===(kept[k].data[0]&15)&&e.data[1]===kept[k].data[1]);
        if(release>=0){const [e]=kept.splice(release,1);kept.splice(k,0,e);k++;}
      }
      i=j;
    }
    const payload=[];let previous=0;
    for(const e of kept){payload.push(...vlq(e.tick-previous),...e.data);previous=e.tick;}
    const header=Buffer.alloc(8);header.write('MTrk');header.writeUInt32BE(payload.length,4);
    chunks.push(header,Buffer.from(payload));
  }
  const output=Buffer.concat(chunks),after=parseMidi(output);
  const key=n=>JSON.stringify([n.startBeat,n.durationBeats,n.pitch,n.velocity,n.channel,!!n.unterminated]);
  for(let ti=0;ti<score.tracks.length;ti++) {
    const original=score.tracks[ti], result=after.tracks[ti],part=owners.get(ti);
    const expected=part?[...original.notes.filter(n=>n.startBeat<startBeat||n.startBeat>=endBeat),...part.notes]:original.notes;
    if(JSON.stringify(expected.map(key).sort())!==JSON.stringify(result.notes.map(key).sort()))
      throw Error('MIDI round-trip changed an unintended note');
  }
  return output;
}
