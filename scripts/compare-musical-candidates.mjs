import {readFileSync} from 'node:fs';
import path from 'node:path';
import {pathToFileURL} from 'node:url';
import {parseMidi} from '../web/shared/midi.mjs';

const read = (dir, name) => JSON.parse(readFileSync(path.join(dir, name), 'utf8'));
const signature = n => [n.startBeat,n.durationBeats,n.pitch,n.velocity,n.channel].join('/');
export function observations(notes, beatsPerBar = 4) {
  if(!Number.isFinite(beatsPerBar)||beatsPerBar<=0)throw Error('Invalid meter');
  const sorted=[...notes].sort((a,b)=>a.startBeat-b.startBeat||a.pitch-b.pitch);
  const median=a=>{a.sort((x,y)=>x-y);return a.length?(a[Math.floor((a.length-1)/2)]+a[Math.floor(a.length/2)])/2:null;};
  const onsets=[...new Set(sorted.map(n=>n.startBeat))];
  const windows=new Map();for(const n of sorted){const w=Math.floor(n.startBeat/(8*beatsPerBar));windows.set(w,(windows.get(w)||0)+1);}
  return {noteCount:notes.length,distinctAttackTimes:onsets.length,
    medianDurationBeats:median(notes.map(n=>n.durationBeats)),
    medianOnsetGapBeats:median(onsets.slice(1).map((t,i)=>t-onsets[i])),
    pitchRange:notes.length?[Math.min(...notes.map(n=>n.pitch)),Math.max(...notes.map(n=>n.pitch))]:null,
    attackCountsPerEightBars:[...windows].sort((a,b)=>a[0]-b[0]),
    lastNotes:sorted.slice(-4)};
}
export function delta(before, after) {
  const counts = notes => {const m = new Map(); for(const n of notes){const k=signature(n);m.set(k,(m.get(k)||0)+1);}return m;};
  const a=counts(before), b=counts(after);let retained=0;
  for(const [key,n] of a)retained+=Math.min(n,b.get(key)||0);
  return {retained,removed:before.length-retained,added:after.length-retained};
}
function load(dir) {
  const manifest=read(dir,'manifest.json'), request=read(dir,'job.json');
  const tracks=manifest.tracks.map(t=>{
    if(path.basename(t.filename)!==t.filename)throw Error('Unsafe MIDI filename');
    return {name:t.name||t.filename,filename:t.filename,notes:parseMidi(readFileSync(path.join(dir,t.filename))).tracks.flatMap(t=>t.notes)};
  });
  const audit=read(dir,'musical-audit.json');
  const meter=read(dir,'composition-plan.json').beats_per_bar;
  return {title:manifest.title,key:manifest.key,request,tracks,meter,
    audit:Object.fromEntries(['harmonic_conflicts','low_register_conflicts','unsupported_chromatic',
      'invalid_sustains','harmonic_floor_coverage','terminal_phrase_ready'].map(k=>[k,audit[k]])),
    engine:read(dir,'experiment-context.json').workerSha256};
}
export function compare(beforeDir,afterDir) {
  const a=load(beforeDir), b=load(afterDir);
  const exactReview=b.request.review_from_job===path.basename(path.resolve(beforeDir));
  return {method:'MIDI evidence, not an aesthetic score',baseline:a.title,candidate:b.title,
    baselineEngine:a.engine,candidateEngine:b.engine,exactReview,
    sameSeed:a.request.seed===b.request.seed,samePrompt:a.request.prompt===b.request.prompt,
    sameKey:a.key===b.key,baselineAudit:a.audit,candidateAudit:b.audit,
    tracks:b.tracks.map((t,i)=>({name:t.name,...observations(t.notes,b.meter),
      matchedIdentity:a.tracks[i]?.name===t.name&&a.tracks[i]?.filename===t.filename,
      delta:exactReview&&a.tracks[i]?.name===t.name&&a.tracks[i]?.filename===t.filename
        ?delta(a.tracks[i].notes,t.notes):null})),
    listeningQuestions:['Does the secondary phrase have an intelligible continuation and arrival?',
      'Do support changes prepare or answer something actually heard?',
      'Is the groove retained without an unintentional attack collision?',
      'Does the ending repay the earlier expectation?',
      'Did the revision damage an attractive earlier passage?'],
    warning:'Different fresh generations are not isolated causal tests. A no-op review is not improvement. Repeat across works and obtain human listening before promotion.'};
}
if(process.argv[1]&&import.meta.url===pathToFileURL(process.argv[1]).href){
  if(process.argv.length!==4)throw Error('Usage: node scripts/compare-musical-candidates.mjs baselineDirectory candidateDirectory');
  console.log(JSON.stringify(compare(...process.argv.slice(2)),null,2));
}
