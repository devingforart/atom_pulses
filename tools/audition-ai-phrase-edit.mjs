// Local research, not a production repair policy. Never updates the source job.
import fs from 'node:fs';
import path from 'node:path';
import { randomUUID } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { parseMidi } from '../web/shared/midi.mjs';
import { replaceMidiPhrases } from './lib/midi-phrase-replacement.mjs';

const [command,argument,studyFile]=process.argv.slice(2);
if(!['generate','materialize','publish'].includes(command)||!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(argument||''))
  throw Error('Usage: audition-ai-phrase-edit.mjs generate <source-job> | materialize <draft> | publish <draft>');
const jobs=path.resolve('build/local-studio-data/jobs'),drafts=path.resolve('build/articulation-experiments');
const openingVoices=['PULSO First violin protagonist','PULSO Second violin motif answerer'];
const read=(dir,file)=>JSON.parse(fs.readFileSync(path.join(dir,file),'utf8'));
const write=(dir,file,value)=>fs.writeFileSync(path.join(dir,file),JSON.stringify(value,null,2));
let id=argument,dir=path.join(drafts,id);
if(command==='generate') {
  if(!process.env.OPENAI_API_KEY)throw Error('Missing OPENAI_API_KEY');
  id=randomUUID();dir=path.join(drafts,id);fs.mkdirSync(dir,{recursive:true});
  const source=path.join(jobs,argument),score=parseMidi(fs.readFileSync(path.join(source,'full-song.mid')));
  const plan=read(source,'composition-plan.json');
  const study=studyFile?JSON.parse(fs.readFileSync(path.resolve(studyFile),'utf8')):null;
  const start=study?.startBeat??4,end=study?.endBeat??16,voices=study?.voices??openingVoices;
  let baseRevision=null;
  if(study?.reviseDraftId){
    if(!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(study.reviseDraftId))throw Error('Invalid base draft');
    const baseDir=path.join(drafts,study.reviseDraftId),baseRequest=read(baseDir,'request.json');
    if(baseRequest.sourceId!==argument||baseRequest.study?.startBeat!==start||baseRequest.study?.endBeat!==end||
      JSON.stringify(baseRequest.study?.voices)!==JSON.stringify(voices))throw Error('Draft revision scope mismatch');
    baseRevision=read(baseDir,'revision.json');
  }
  if(!Number.isFinite(start)||!Number.isFinite(end)||start<0||end<=start||end>score.lengthBeats||
    !Array.isArray(voices)||!voices.length||new Set(voices).size!==voices.length||
    voices.some(name=>score.tracks.filter(t=>t.name===name).length!==1))throw Error('Invalid study scope');
  const instructions=study?`You are editing actual written MIDI in an isolated listening experiment, not changing the production composer.
The user found a concrete musical problem. Return complete replacement notes for the specified voices and interval;
do not change any other voice, tempo, bar, or the accepted opening phrase. Every revised note must be authored by you.
Use the supplied actual pitches/rhythms and ensemble/harmony as evidence, not an imagined ideal score.
Notes use zero-based quarter beats, C4=60, 120 BPM, 4 beats/bar, multiples of 0.25.
Replacement onsets must be >=${start} and <${end}; all replacements must release by ${end}.
No overlapping notes inside a single voice; inter-voice overlap is allowed and often valuable.
You may choose the note count, pitches, timing and velocity; preserve the composition's theme and musical identity.
Do not mechanically thin, quantize, stretch or impose uniform quarter notes. Do not substitute prose for music.
The user likes the complexity in bar 53, so ALL of that passage is immutable, as are the improved bars 2-4.
Consider audition of ALL tracks through one piano: accidental same-pitch reattacks may obscure a sentence,
but deliberate repetition, contrapuntal overlap, syncopation and virtuosity are not intrinsically defects.
${Number.isInteger(study.harmonicSectionIndex)?
'You own all pitched parts in this interval and may revise its harmony in the existing tonal world. Return harmonicEvents using the existing chord palette. Write consistent ACTUAL notes in every affected accompaniment part; a harmonic label alone changes nothing. Keep every other section and every percussion event unchanged.':
'Preserve the supplied harmony and fit the unchanged accompaniment, including actual sustained pitches across the interval.'}
The following is this experiment's listening commission, not a permanent rule or genre template:
${baseRevision?'This is a focused revision of proposedRevision. Return ONLY the requested editableVoiceFields; every other proposed note and its harmonic timeline is immutable, not a suggestion to rewrite.':''}
${study.instructions}
Return exact notes and a concise Spanish explanation of the ACTUAL change. You have symbolic evidence, not audio.`:
`Edit the actual musical sentence, not a prose description. The listener hears bars 2-4 as clumsy,
particularly bar 3, auditioning ALL voices on one piano. A previous timing-only edit failed: it shortened the
first D4 at beat 7.75 to 0.25 beat, kept the D4 reattack at 8.5 and staggered the answering voice further.
Do NOT repeat that failed solution. You may now replace notes, consolidate a repeated pitch into a sustained
utterance, omit an unnecessary attack, or reshape the related answer when musically justified. Every note is your decision.
Keep the opening G4-F4-E4-A4 thematic identity and its meaningful D-centered continuation. Do not replace it
with generic scales or an unrelated theme. The goal is a connected, memorable, rhythmically intelligible
foreground sentence with an answer that listeners can distinguish even when collapsed into one piano.
Consider the combined attack stream and SAME-pitch near reattacks, not only each voice separately.
Keep meaningful energy and expressive rhythm. Not everything needs to be legato, slow, synchronized, or consonant.
Avoid replacing the awkwardness with another weak pickup/double tap, constant quarter notes, arbitrary
sixteenth-note displacement, a blank answering voice, or an empty phrase. Make the approach to and departure
from bar 3 understandable, with a purposeful resting/arriving melodic tone rather than a stumbling repeated one.
Write BOTH complete replacement voices for the zero-based beat interval [4,16), i.e. bars 2-4 in 4/4 at 120 BPM.
You may choose note count, pitch, rhythm, duration and velocity within that interval. Keep existing harmony D minor
and register/contour compatible with the surrounding original voices. Before beat 4 and from beat 16 onward,
ALL notes are immutable; so are ALL accompanying tracks everywhere. End every replacement note by beat 16.
The notes at beat 0..4 introduce the thought; the next unedited first violin A4 at beat 16 must feel like its continuation.
Use precise quarter-note beat coordinates (not bars), multiples of 0.25. No overlapping notes within a single violin.
Return a concise Spanish account referring to your ACTUAL new notes. You have symbolic evidence, not audio.`;
  const note={type:'object',additionalProperties:false,required:['startBeat','durationBeats','pitch','velocity'],properties:{
    startBeat:{type:'number',minimum:start,maximum:end-0.25,multipleOf:0.25},
    durationBeats:{type:'number',minimum:0.25,maximum:end-start,multipleOf:0.25},
    pitch:{type:'integer',minimum:0,maximum:127},velocity:{type:'integer',minimum:1,maximum:127}}};
  const allFields=study?voices.map((_,i)=>`voice_${i}`):['protagonist','answer'];
  const fields=baseRevision?study.editableVoiceFields:allFields;
  if(!Array.isArray(fields)||!fields.length||fields.some(f=>!allFields.includes(f)))throw Error('Invalid editable voice fields');
  const schema={type:'object',additionalProperties:false,required:['explanation',...fields],properties:{
    explanation:{type:'string'},...Object.fromEntries(fields.map(key=>[key,{type:'array',maxItems:128,items:note}]))}};
  if(Number.isInteger(study?.harmonicSectionIndex)&&!baseRevision){
    const section=plan.sections[study.harmonicSectionIndex];
    if(!section||section.start_bar*score.beatsPerBar!==start||
       (section.start_bar+section.bars)*score.beatsPerBar!==end||
       score.tracks.some(t=>t.notes.some(n=>n.channel!==9&&n.startBeat<end&&n.startBeat+n.durationBeats>start)&&!voices.includes(t.name)))
      throw Error('Harmonic revision must own the complete section and all sounding pitched parts');
    schema.required.push('harmonicEvents','sectionPurpose');
    schema.properties.sectionPurpose={type:'string'};
    schema.properties.harmonicEvents={type:'array',minItems:1,maxItems:16,items:{type:'object',additionalProperties:false,
      required:['startBeat','chordId','purpose'],properties:{startBeat:{type:'number',minimum:start,maximum:end-0.25,multipleOf:0.25},
        chordId:{type:'string',enum:plan.chord_palette.map(c=>c.id)},purpose:{type:'string'}}}};
  }
  const input={original:score.tracks.filter(t=>t.notes.length).map(t=>({name:t.name,
    notes:t.notes.filter(n=>study?(n.startBeat<32||(n.startBeat<end+16&&n.startBeat+n.durationBeats>start-16)||
      /violin|Clarinet/i.test(t.name)):n.startBeat<32).map(n=>[n.startBeat,n.durationBeats,n.pitch,n.velocity])})),
    voiceFields:Object.fromEntries(allFields.map((field,i)=>[field,voices[i]])),
    ...(baseRevision?{proposedRevision:baseRevision,editableVoiceFields:fields}:{}),
    sourceTheme:plan.motif_identity,key:plan.key,...(Number.isInteger(study?.harmonicSectionIndex)?{chordPalette:plan.chord_palette}:{})};
  const headers={Authorization:`Bearer ${process.env.OPENAI_API_KEY}`,'Content-Type':'application/json'};
  const request=async(url,options={})=>{
    const r=await fetch(url,{...options,headers,signal:AbortSignal.timeout(60000)});
    if(!r.ok)throw Error(`AI HTTP ${r.status}; no automatic resubmission`);return r.json();};
  let response=await request('https://api.openai.com/v1/responses',{method:'POST',body:JSON.stringify({
    model:'gpt-5.6-terra',reasoning:{effort:'medium'},background:true,max_output_tokens:study?10000:3500,
    instructions,input:JSON.stringify(input),text:{format:{type:'json_schema',name:'connected_phrase_edit',strict:true,schema}}})});
  write(dir,'request.json',{sourceId:argument,responseId:response.id,instructions,input,...(study?{study}:{})});
  console.log(JSON.stringify({draft:id,responseId:response.id}));
  const started=Date.now();
  while(['queued','in_progress'].includes(response.status)){
    if(Date.now()-started>600000)throw Error('Remote response still pending; retrieve saved response, do not resubmit');
    await delay(2500);response=await request(`https://api.openai.com/v1/responses/${response.id}`);
  }
  write(dir,'response.json',response);
  if(response.status!=='completed')throw Error(`AI response ${response.status}`);
  const partial=JSON.parse(response.output.flatMap(o=>o.content||[]).filter(c=>c.type==='output_text').map(c=>c.text).join(''));
  const revision=baseRevision?{...baseRevision,...partial}:partial;
  write(dir,'revision.json',revision);
}
const request=read(dir,'request.json'),revision=read(dir,'revision.json');
const source=path.join(jobs,request.sourceId),sourceMidi=fs.readFileSync(path.join(source,'full-song.mid'));
const original=parseMidi(sourceMidi);
const draftsToApply=[{id,request,revision}];
if(command==='publish'&&studyFile){
  if(!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(studyFile)||studyFile===id)throw Error('Invalid companion draft');
  const other=path.join(drafts,studyFile),otherRequest=read(other,'request.json');
  if(otherRequest.sourceId!==request.sourceId)throw Error('Companion must refer to the same immutable source');
  draftsToApply.push({id:studyFile,request:otherRequest,revision:read(other,'revision.json')});
}
const regions=draftsToApply.map(draft=>{
 const request=draft.request,revision=draft.revision;
 const start=request.study?.startBeat??4,end=request.study?.endBeat??16,voices=request.study?.voices??openingVoices;
 const replacements=voices.map((name,i)=>{
  const track=original.tracks.find(t=>t.name===name);
  if(!track?.notes.length)throw Error('Missing source voice');
  const notes=revision[request.study?`voice_${i}`:i===0?'protagonist':'answer'];
  for(let k=0;k<notes.length;k++){
    const n=notes[k];
    if(!Number.isInteger(n.startBeat*4)||!Number.isInteger(n.durationBeats*4))throw Error('AI note outside representation grid');
    if(k&&n.startBeat<notes[k-1].startBeat)throw Error('AI voice reordered; no automatic fix');
    if(k&&!request.study?.polyphonicVoices?.includes(name)&&n.startBeat<notes[k-1].startBeat+notes[k-1].durationBeats)
      throw Error('AI monophonic voice overlapping; no automatic fix');
    if(notes.slice(0,k).some(p=>p.pitch===n.pitch&&p.startBeat+p.durationBeats>n.startBeat))throw Error('AI same-pitch note overlap');
  }
  return {name,notes:notes.map(n=>({...n,channel:track.notes[0].channel}))};
 });
 let harmony=null;
 if(Number.isInteger(request.study?.harmonicSectionIndex)){
   const plan=read(source,'composition-plan.json'),events=revision.harmonicEvents;
   if(!Array.isArray(events)||!events.length||events[0].startBeat!==start||events.some((e,i)=>
     !Number.isFinite(e.startBeat)||e.startBeat<start||e.startBeat>=end||!Number.isInteger(e.startBeat*4)||
     !plan.chord_palette.some(c=>c.id===e.chordId)||(i&&e.startBeat<=events[i-1].startBeat)))throw Error('Invalid AI harmonic timeline');
   harmony={sectionIndex:request.study.harmonicSectionIndex,events,purpose:revision.sectionPurpose};
 }
 return {draftId:draft.id,start,end,voices,replacements,harmony,explanation:revision.explanation};
});
for(let i=0;i<regions.length;i++)for(let j=i+1;j<regions.length;j++){
  const a=regions[i],b=regions[j];
  if(a.start<b.end&&b.start<a.end&&a.voices.some(v=>b.voices.includes(v)))throw Error('Overlapping study scopes; no implicit precedence');
}
let full=sourceMidi;
for(const region of regions)full=replaceMidiPhrases(full,region.replacements,region.start,region.end);
const parsed=parseMidi(full);
// Publishing a compound comparison never replaces either single-region draft.
if(draftsToApply.length===1)fs.writeFileSync(path.join(dir,'full-song.mid'),full);
const total=parsed.tracks.reduce((s,t)=>s+t.notes.length,0);
const audit={sourceId:request.sourceId,explanation:revision.explanation,
  notes:total,regions,pitchesAndRhythmsAuthoredByAI:true,
  outsideScopeUnchanged:true,qualityEstablishedByListening:false,
  replacedNotes:regions.reduce((sum,r)=>sum+original.tracks.filter(t=>r.voices.includes(t.name)).reduce((s,t)=>s+t.notes.filter(n=>n.startBeat>=r.start&&n.startBeat<r.end).length,0),0),
  replacementNotes:regions.reduce((sum,r)=>sum+r.replacements.reduce((s,t)=>s+t.notes.length,0),0)};
if(draftsToApply.length===1)write(dir,'audit.json',audit);
if(command==='publish'){
  const destination=path.join(jobs,id);fs.mkdirSync(destination,{recursive:false});
  for(const name of ['composition-plan.json','phrase-architecture.json'])fs.copyFileSync(path.join(source,name),path.join(destination,name));
  const plan=read(source,'composition-plan.json');
  for(const region of regions)if(region.harmony){
    const section=plan.sections[region.harmony.sectionIndex];
    section.harmonic_events=region.harmony.events.map(e=>({absolute_beat:e.startBeat,
      bar_offset:Math.floor((e.startBeat-region.start)/4),beat_offset:(e.startBeat-region.start)%4,
      chord_id:e.chordId,purpose:e.purpose}));
    section.harmonic_direction=region.harmony.purpose;
    section.motif_treatment=region.explanation;
  }
  plan.local_audition_revision={sourceJob:request.sourceId,drafts:draftsToApply.map(d=>d.id),
    noteAuthority:'full-song.mid',originalPerformanceCellsAreHistorical:true};
  write(destination,'composition-plan.json',plan);
  write(destination,'phrase-edit-experiment.json',audit);
  fs.writeFileSync(path.join(destination,'full-song.mid'),full);
  const manifest=read(source,'manifest.json');
  for(const t of manifest.tracks){
    let output=fs.readFileSync(path.join(source,t.filename));
    for(const region of regions){
      const selected=region.replacements.filter(r=>r.name===`PULSO ${t.name}`);
      if(selected.length)output=replaceMidiPhrases(output,selected,region.start,region.end);
    }
    fs.writeFileSync(path.join(destination,t.filename),output);
    const trackNotes=parseMidi(output).tracks.flatMap(x=>x.notes);
    const fullNotes=parsed.tracks.find(x=>x.name===`PULSO ${t.name}`).notes;
    if(JSON.stringify(trackNotes)!==JSON.stringify(fullNotes))throw Error('Individual/full mismatch');
    t.notes=trackNotes.length;
  }
  const job=read(source,'job.json');
  write(destination,'job.json',{...job,created_at:new Date().toISOString(),first_pass_audition:false,
    resume_checkpoint_file:null,foreground_from_job:null,phrase_edit_from_job:request.sourceId,
    phrase_edit_drafts:draftsToApply.map(d=>d.id)});
  write(destination,'local-origin.json',{origin:'auto'});
  // The source's old quality/audit scores do not describe this edited MIDI.
  delete manifest.editorial;
  write(destination,'manifest.json',{...manifest,title:request.study?.title??'The Fourth That Returns — C · frase conectada',
    first_pass_audition:false,thematic_rehearsal:false,phrase_edit_from_job:request.sourceId,
    all_midi_notes:total,ai_authored_or_declared_transform_notes:total,
    note_provenance:{direct_ai:total,reused_ai:0,transformed_ai:0,reused_transformed_ai:0,other:0}});
}
console.log(JSON.stringify({draft:id,source:request.sourceId,explanation:revision.explanation,
  regions, ...(command==='publish'?{url:`http://localhost:4198/?job=${id}`}:{published:false})}));
