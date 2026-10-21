// Read-only evidence. No grade, musical gate, generated notes or automatic edits.
import fs from 'node:fs';
import path from 'node:path';
import { parseMidi } from '../web/shared/midi.mjs';
const id=process.argv[2];
if(!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(id||''))throw Error('Provide local job UUID');
const directory=path.resolve('build/local-studio-data/jobs',id);
const read=name=>JSON.parse(fs.readFileSync(path.join(directory,name),'utf8'));
const manifest=read('manifest.json'),plan=read(manifest.planFile||'composition-plan.json');
const score=parseMidi(fs.readFileSync(path.join(directory,manifest.fullFile||'full-song.mid')));
const phrase=fs.existsSync(path.join(directory,'phrase-architecture.json'))?read('phrase-architecture.json'):{};
const sum=xs=>xs.reduce((a,b)=>a+b,0),hist=xs=>xs.reduce((h,x)=>(h[x]=(h[x]||0)+1,h),{});
const tracks=score.tracks.filter(t=>t.notes.length);
const facts=notes=>({notes:notes.length,durationHistogram:hist(notes.map(n=>n.durationBeats)),
  fractionalOnsets:notes.filter(n=>Math.abs(n.startBeat-Math.round(n.startBeat))>1e-8).length,
  pitchRange:notes.length?[Math.min(...notes.map(n=>n.pitch)),Math.max(...notes.map(n=>n.pitch))]:null,
  meanVelocity:notes.length?sum(notes.map(n=>n.velocity))/notes.length:null});
const references=[...(phrase.musical_subject?[{id:'primary',...phrase.musical_subject}]:[]),...(phrase.thematic_materials||[])];
const materialEvidence=references.map(material=>{
 const owner=plan.cast.find(p=>p.id===material.instrument_id);
 const track=owner&&tracks.find(t=>t.name===owner.name||t.name===`PULSO ${owner.name}`);
 const reference=material.notes||[],matches=[];
 // Exact rhythmic correspondence with optional constant transposition. A changed
 // continuation can be valid development without satisfying this narrow match.
 if(track&&reference.length)for(let i=0;i+reference.length<=track.notes.length;i++){
  const actual=track.notes.slice(i,i+reference.length),first=actual[0],ref=reference[0];
  const transpose=first.pitch-ref.pitch;
  if(actual.every((n,j)=>n.pitch-reference[j].pitch===transpose&&
      Math.abs(n.startBeat-first.startBeat-reference[j].beat+ref.beat)<1e-8&&
      Math.abs(n.durationBeats-reference[j].duration)<1e-8))
    matches.push({beat:first.startBeat,seconds:score.beatToSeconds(first.startBeat),transpose});
 }
 return {id:material.id,owner:material.instrument_id,referenceNotes:reference.length,
  referenceDuration:material.length_beats,exactRhythmicStatementMatches:matches,
  limitation:'A missing exact match is not proof of missing development; inspect actual musical continuation.'};
});
const repetitions=tracks.map(track=>{
 const groups=new Map(),size=7;
 for(let i=0;i+size<=track.notes.length;i++){
  const notes=track.notes.slice(i,i+size),start=notes[0].startBeat;
  const key=notes.map(n=>[n.startBeat-start,n.pitch,n.durationBeats].join('/')).join(';');
  if(!groups.has(key))groups.set(key,[]);groups.get(key).push(start);
 }
 return {name:track.name,sevenEventSequencesRepeatedAtLeastThreeTimes:[...groups.values()]
  .filter(g=>g.length>=3).sort((a,b)=>b.length-a.length).slice(0,6)};
});
const report={jobId:id,title:manifest.title,advisoryOnly:true,musicalQualityEstablished:false,
 method:'Actual MIDI timing, duration, register, dynamics and narrow reference matching. No audio listening or automatic musical verdict.',
 durationSeconds:score.beatToSeconds(score.lengthBeats),
 noteProvenance:manifest.note_provenance,tracks:tracks.map(t=>({name:t.name,...facts(t.notes)})),
 materialEvidence,repetitions,sections:plan.sections.map((section,index)=>{
  const begin=section.start_bar*score.beatsPerBar,end=begin+section.bars*score.beatsPerBar;
  const parts=tracks.map(t=>({name:t.name,...facts(t.notes.filter(n=>n.startBeat>=begin&&n.startBeat<end))}));
  return {index,startSeconds:score.beatToSeconds(begin),lengthSeconds:score.beatToSeconds(end)-score.beatToSeconds(begin),
   parts,plannedTasks:(phrase.phrases||[]).filter(p=>p.section_index===index).map(p=>({
    localStartBar:p.local_start_bar,bars:p.bars,development:p.material_development||null}))};
 })};
const output=process.argv.indexOf('--output');
if(output>=0){if(!process.argv[output+1])throw Error('Missing output path');fs.writeFileSync(path.resolve(process.argv[output+1]),JSON.stringify(report,null,2)+'\n');}
else console.log(JSON.stringify(report,null,2));
