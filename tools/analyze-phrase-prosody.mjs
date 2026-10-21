// Read-only evidence, never a musical quality score or an automatic repair.
import fs from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../web/shared/midi.mjs';
const id=process.argv[2];
if(!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(id||''))throw Error('Provide local job UUID');
const directory=path.resolve('build/local-studio-data/jobs',id);
const read=name=>JSON.parse(fs.readFileSync(path.join(directory,name),'utf8'));
const manifest=read('manifest.json'),plan=read('composition-plan.json');
const score=parseMidi(fs.readFileSync(path.join(directory,manifest.fullFile||'full-song.mid')));
const phrase=fs.existsSync(path.join(directory,'phrase-architecture.json'))?read('phrase-architecture.json'):{};
const argument=(name,fallback)=>{const at=process.argv.indexOf(name);return at<0?fallback:Number(process.argv[at+1]);};
const start=argument('--start',0),end=argument('--end',score.lengthBeats);
if(!Number.isFinite(start)||!Number.isFinite(end)||start<0||end<=start)throw Error('Invalid interval');
const position=beat=>`${Math.floor(beat/score.beatsPerBar)+1}.${Math.floor(beat%score.beatsPerBar)+1}.${Math.round((beat%1)*4)+1}`;
const parts=score.tracks.filter(t=>t.notes.length).map(track=>{
 const notes=track.notes.filter(n=>n.startBeat>=start&&n.startBeat<end);
 const histogram={};for(const n of notes){const key=String(Math.round(n.startBeat%1*4)/4);histogram[key]=(histogram[key]||0)+1;}
 const monophonic=notes.every((n,i)=>!i||n.startBeat>=notes[i-1].startBeat+notes[i-1].durationBeats-1e-8);
 const sameKeyOverlaps=[];
 for(let i=0;i<notes.length;i++)for(let j=i+1;j<notes.length&&notes[j].startBeat<notes[i].startBeat+notes[i].durationBeats-1e-8;j++){
  if(notes[i].pitch!==notes[j].pitch||notes[i].channel!==notes[j].channel)continue;
  sameKeyOverlaps.push({pitch:notes[i].pitch,first:position(notes[i].startBeat),second:position(notes[j].startBeat),
   overlapBeats:Math.min(notes[i].startBeat+notes[i].durationBeats,notes[j].startBeat+notes[j].durationBeats)-notes[j].startBeat,
   interpretation:'Same track/key/channel reattack while held; inspect intent and playback. Not an automatic rejection or edit.'});
 }
 return {name:track.name,pitched:track.notes.some(n=>n.channel!==9),noteCount:notes.length,monophonic,onsetFractionHistogram:histogram,
  sameKeyOverlaps,
  entries:notes.slice(0,12).map(n=>({position:position(n.startBeat),beat:n.startBeat,pitch:n.pitch,
    duration:n.durationBeats,seconds:score.beatToSeconds(n.startBeat),velocity:n.velocity})),
  shortInternalGaps:monophonic?notes.slice(1).flatMap((n,i)=>{
    const gap=n.startBeat-notes[i].startBeat-notes[i].durationBeats;
    return gap>1e-8&&gap<=0.75?[{previous:position(notes[i].startBeat),next:position(n.startBeat),gapBeats:gap}]:[];
  }):[],notes};
});
const shadows=[];
for(let i=0;i<parts.length;i++)for(let j=i+1;j<parts.length;j++){
 const a=parts[i],b=parts[j];if(!a.pitched||!b.pitched||!a.monophonic||!b.monophonic)continue;
 for(let x=0;x<a.notes.length;x++)for(let y=0;y<b.notes.length;y++){
  const offset=b.notes[y].startBeat-a.notes[x].startBeat;
  if(Math.abs(offset)<1e-8||Math.abs(offset)>1||a.notes[x].pitch!==b.notes[y].pitch)continue;
  if(x&&y&&a.notes[x-1].pitch===b.notes[y-1].pitch&&
     Math.abs(b.notes[y-1].startBeat-a.notes[x-1].startBeat-offset)<1e-8)continue;
  let count=0;
  while(x+count<a.notes.length&&y+count<b.notes.length&&a.notes[x+count].pitch===b.notes[y+count].pitch&&
    Math.abs(b.notes[y+count].startBeat-a.notes[x+count].startBeat-offset)<1e-8)count++;
  if(count>=3)shadows.push({parts:[a.name,b.name],positions:[position(a.notes[x].startBeat),position(b.notes[y].startBeat)],
    offsetBeats:offset,matchingNotes:count,pitches:a.notes.slice(x,x+count).map(n=>n.pitch),
    interpretation:'Evidence only: may be intentional imitation; inspect phrasing and listen.'});
 }
}
const spans=(phrase.phrases||[]).flatMap(p=>{
 const section=plan.sections[p.section_index];if(!section)return[];
 const phraseStart=(section.start_bar+p.local_start_bar)*score.beatsPerBar;
 return(p.ensemble_spans||[]).flatMap(s=>{
  const begin=phraseStart+s.beat,finish=begin+s.duration;
  return begin<end&&finish>start?[{start:begin,end:finish,jointOwners:s.joint_voice_ids||[],
   foreground:s.foreground_ids,pulseCarrier:s.pulse_carrier_id,rhythmicIdentity:s.rhythmic_identity,
   articulation:s.articulation_intent,supports:(s.metric_supports||[]).map(a=>({beat:begin+a.beat,function:a.function}))}]:[];
 });
});
const jointIds=[...new Set(spans.flatMap(s=>s.jointOwners))];
const foreground=fs.existsSync(path.join(directory,'partial-01.mid'))?
 parseMidi(fs.readFileSync(path.join(directory,'partial-01.mid'))).tracks.filter(t=>t.notes.length).map(t=>t.name):[];
const noteEnd=Math.max(0,...score.tracks.flatMap(t=>t.notes.map(n=>n.startBeat+n.durationBeats)));
const report={jobId:id,title:manifest.title,advisoryOnly:true,musicalQualityEstablished:false,
 method:'Exact MIDI onsets/durations and AI-declared relationships; no listening, grading, rejection or note edits.',
 interval:{startBeat:start,endBeat:end},plannedSpans:spans,jointOwners:jointIds,
 completeDialogueParts:foreground,
 plannedJointOwnersPresentInDialogue:jointIds.map(id=>({id,present:foreground.some(name=>{
  const owner=plan.cast.find(p=>p.id===id);return owner&&(name===owner.name||name===`PULSO ${owner.name}`);
 })})),
 parts:parts.map(({notes,...part})=>part),offsetPitchStreamMatches:shadows,
 finalRelease:{beat:noteEnd,seconds:score.beatToSeconds(noteEnd),tailSeconds:score.beatToSeconds(score.lengthBeats)-score.beatToSeconds(noteEnd)}};
const out=process.argv.indexOf('--output');
if(out>=0){if(!process.argv[out+1])throw Error('Missing output path');fs.writeFileSync(path.resolve(process.argv[out+1]),JSON.stringify(report,null,2));}
else console.log(JSON.stringify(report,null,2));
