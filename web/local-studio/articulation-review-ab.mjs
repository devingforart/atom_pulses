// Local, opt-in A/B: AI revises note durations only. Original MIDI is immutable;
// no procedural lengthening, pitch generation, attack movement, or note deletion.
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { randomUUID } from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseMidi } from '../shared/midi.mjs';
import { changeMidiNoteDurations } from '../shared/midi-edit.mjs';
import { jobDirectory, jobsRoot } from './storage.mjs';

const option = name => process.argv.find(value => value.startsWith(`--${name}=`))?.slice(name.length + 3);
const readJson = file => JSON.parse(readFileSync(file, 'utf8'));
const almost = (a, b) => Math.abs(a - b) < .001;

export function foregroundTrackIndex(manifest, plan) {
  const owner = plan.protagonist_id;
  const partIndex = plan.cast?.findIndex(part => part.id === owner);
  if (partIndex < 0 || !manifest.tracks?.[partIndex])
    throw new Error('The AI protagonist track is missing');
  return partIndex + 1; // Type 1 MIDI track zero contains tempo and meter.
}

export function candidateIndices(notes) {
  return notes.flatMap((note, index) => note.durationBeats <= 1.0 + .001 ? [index] : []);
}

function leadLedger(notes, beatsPerBar, chords) {
  return notes.map((note, index) => {
    const next = notes[index + 1];
    const chord = chords.findLast(event => event.beat <= note.startBeat + .001);
    return { index, bar: +(1 + note.startBeat / beatsPerBar).toFixed(3),
      start: note.startBeat, pitch: note.pitch, duration: note.durationBeats,
      velocity: note.velocity, gap_to_next: next ?
        +(next.startBeat - note.startBeat - note.durationBeats).toFixed(3) : null,
      chord: chord?.chord || '' };
  });
}

function outputText(response) {
  return response.output?.flatMap(item => item.content || [])
    .filter(item => item.type === 'output_text').map(item => item.text).join('') || '';
}

async function requestArticulation(context) {
  const key = process.env.OPENAI_API_KEY;
  if (!key) throw new Error('OPENAI_API_KEY is not configured');
  const schema = { type: 'object', additionalProperties: false,
    properties: { summary: { type: 'string' }, changes: { type: 'array', items: {
      type: 'object', additionalProperties: false, properties: {
        note_index: { type: 'integer' }, duration_beats: { type: 'number' },
        reason: { type: 'string' },
      }, required: ['note_index', 'duration_beats', 'reason'],
    } } }, required: ['summary', 'changes'] };
  const instructions = [
    'You are an expert expressive MIDI editor, not a new composer. A listener hears some foreground notes ending before the phrase feels complete.',
    'Make a selective articulation revision of the existing protagonist ONLY. Return duration changes by note index. Keep every note onset, pitch, velocity, order and all other tracks exactly as written.',
    'Inspect melody, harmonic context and authored phrase plan. Preserve expressive short pickups and rests when they lead somewhere. Do not extend every short note or quantize the performance.',
    'Pay special attention to the E attacks near bars 3 and 4.4: decide musically whether their releases support a coherent phrase, and revise when they sound inadvertently clipped.',
    'A short detached phrase ending followed by a long disappearance deserves scrutiny, but silence may be intentional. Prefer a meaningful held destination or a legato handoff where the original articulation is unconvincing.',
    'Only change notes whose current duration is at most one beat. Do not rewrite more than 20 notes. Changed durations must be in quarter-beat or eighth-beat increments; do not overlap the next melodic attack by more than one eighth of a beat.',
    'Do not sustain an unstable note into a conflicting harmony. Leave a note unchanged if no musically safe improvement exists. Give a specific musical reason for each change.',
    `SCORE CONTEXT ${JSON.stringify(context)}`,
  ].join('\n');
  const body = { model: process.env.PULSO_ARTICULATION_MODEL || 'gpt-5.6-terra',
    reasoning: { effort: 'medium' }, background: true, max_output_tokens: 3200,
    input: instructions,
    text: { format: { type: 'json_schema', name: 'pulso_articulation_review',
      strict: true, schema } } };
  const headers = { Authorization: `Bearer ${key}`, 'Content-Type': 'application/json' };
  let response = await fetch('https://api.openai.com/v1/responses', { method: 'POST',
    headers, body: JSON.stringify(body), signal: AbortSignal.timeout(30000) });
  let result = await response.json();
  if (!response.ok) throw new Error(`AI articulation request failed (${response.status}): ${result.error?.code || result.error?.type || 'unknown'}`);
  const deadline = Date.now() + 240000;
  while (['queued', 'in_progress'].includes(result.status) && Date.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, 5000));
    response = await fetch(`https://api.openai.com/v1/responses/${encodeURIComponent(result.id)}`,
      { headers: { Authorization: `Bearer ${key}` }, signal: AbortSignal.timeout(30000) });
    result = await response.json();
    if (!response.ok) throw new Error(`AI articulation poll failed (${response.status})`);
  }
  if (result.status !== 'completed') throw new Error(`AI articulation ${result.status}; original unchanged`);
  const text = outputText(result);
  if (!text) throw new Error('AI articulation produced no content; original unchanged');
  return { answer: JSON.parse(text), responseId: result.id, usage: result.usage || null };
}

function semitoneExposure(note, score, leadIndex, duration) {
  const start = note.startBeat, end = start + duration;
  let exposure = 0;
  for (const [index, track] of score.tracks.entries()) {
    if (index === 0 || index === leadIndex) continue;
    for (const other of track.notes) {
      if (Math.abs(other.pitch - note.pitch) !== 1) continue;
      const overlap = Math.min(end, other.startBeat + other.durationBeats) -
        Math.max(start, other.startBeat);
      if (overlap > 0) exposure += overlap;
    }
  }
  return exposure;
}

export function validateArticulationChanges(answer, score, leadIndex) {
  const notes = score.tracks[leadIndex]?.notes;
  if (!notes || !Array.isArray(answer?.changes) || !answer.changes.length ||
      answer.changes.length > 20) throw new Error('AI articulation returned no usable bounded edits');
  const seen = new Set(), accepted = [];
  for (const change of answer.changes) {
    const index = change.note_index, note = notes[index], next = notes[index + 1];
    const duration = change.duration_beats;
    if (!Number.isInteger(index) || !note || seen.has(index) ||
        note.durationBeats > 1.001 || !Number.isFinite(duration) ||
        duration < .125 || duration > 8 || almost(duration, note.durationBeats) ||
        !almost(duration * 8, Math.round(duration * 8)) ||
        note.startBeat + duration > score.lengthBeats + .001 ||
        (next && note.startBeat + duration > next.startBeat + .125 + .001))
      throw new Error(`AI articulation changed an unsafe or ineligible note: ${index}`);
    const before = semitoneExposure(note, score, leadIndex, note.durationBeats);
    const after = semitoneExposure(note, score, leadIndex, duration);
    if (after > before + .25 + .001)
      throw new Error(`AI articulation introduces a sustained semitone: ${index}`);
    seen.add(index);
    accepted.push({ trackIndex: leadIndex, noteIndex: index,
      durationBeats: duration, before: note.durationBeats,
      beat: note.startBeat, pitch: note.pitch,
      reason: String(change.reason || '').slice(0, 250) });
  }
  return accepted;
}

function verifyRevision(before, after, leadIndex, edits) {
  if (before.tracks.length !== after.tracks.length ||
      !almost(before.lengthBeats, after.lengthBeats))
    throw new Error('Revised MIDI changed the arrangement structure');
  const wanted = new Map(edits.map(edit => [edit.noteIndex, edit.durationBeats]));
  for (let trackIndex = 0; trackIndex < before.tracks.length; trackIndex++) {
    const left = before.tracks[trackIndex].notes, right = after.tracks[trackIndex].notes;
    if (left.length !== right.length) throw new Error('Revised MIDI changed note count');
    for (let index = 0; index < left.length; index++) {
      const a = left[index], b = right[index];
      if (!almost(a.startBeat, b.startBeat) || a.pitch !== b.pitch ||
          a.velocity !== b.velocity || a.channel !== b.channel ||
          !almost(b.durationBeats,
            trackIndex === leadIndex && wanted.has(index) ? wanted.get(index) : a.durationBeats))
        throw new Error(`Revised MIDI changed an unapproved event ${trackIndex}:${index}`);
    }
  }
}

async function main() {
  const sourceId = option('source');
  const directory = sourceId && jobDirectory(sourceId);
  if (!directory || !existsSync(path.join(directory, 'manifest.json')) ||
      !existsSync(path.join(directory, 'full-song.mid')))
    throw new Error('A completed source job is required (--source=<job-id>)');
  const manifest = readJson(path.join(directory, 'manifest.json'));
  const plan = readJson(path.join(directory, 'composition-plan.json'));
  const sourceBytes = readFileSync(path.join(directory, 'full-song.mid'));
  const score = parseMidi(sourceBytes);
  const leadIndex = foregroundTrackIndex(manifest, plan);
  const track = score.tracks[leadIndex];
  const phrasePlanFile = path.join(directory, 'phrase-architecture.json');
  const phrasePlan = existsSync(phrasePlanFile) ? readJson(phrasePlanFile) : null;
  const chords = plan.sections.flatMap(section => section.harmonic_events || [])
    .map(event => ({ beat: event.absolute_beat, chord: event.chord_id }))
    .sort((a, b) => a.beat - b.beat);
  const context = { title: manifest.title, key: manifest.key, bpm: manifest.bpm,
    lead: track.name, articulation: readJson(path.join(directory,
      manifest.tracks[leadIndex - 1].filename.replace(/\.mid$/i, '.pulso.json'))).parts[0].articulation,
    chord_palette: plan.chord_palette?.map(chord => ({ id: chord.id, pitch_classes: chord.pitch_classes })),
    section_narrative: plan.sections.map(section => ({ name: section.name,
      start_bar: section.start_bar, bars: section.bars, motif_treatment: section.motif_treatment })),
    phrase_plan: phrasePlan?.phrases?.filter(phrase => phrase.speaker_id === plan.protagonist_id),
    notes: leadLedger(track.notes, score.beatsPerBar, chords),
  };
  const { answer, responseId, usage } = await requestArticulation(context);
  const edits = validateArticulationChanges(answer, score, leadIndex);
  const candidateBytes = changeMidiNoteDurations(sourceBytes, edits);
  const candidate = parseMidi(candidateBytes);
  verifyRevision(score, candidate, leadIndex, edits);

  const trackFilename = manifest.tracks[leadIndex - 1].filename;
  const trackBytes = readFileSync(path.join(directory, trackFilename));
  const singleScore = parseMidi(trackBytes);
  const singleLead = singleScore.tracks.findIndex(part => part.notes.length === track.notes.length &&
    part.name === track.name);
  if (singleLead < 0) throw new Error('The isolated lead MIDI differs from full-song.mid');
  const isolated = changeMidiNoteDurations(trackBytes,
    edits.map(edit => ({ trackIndex: singleLead, noteIndex: edit.noteIndex,
      durationBeats: edit.durationBeats })));
  const isolatedNotes = parseMidi(isolated).tracks[singleLead].notes;
  if (isolatedNotes.length !== candidate.tracks[leadIndex].notes.length ||
      isolatedNotes.some((note, index) => !almost(note.durationBeats,
        candidate.tracks[leadIndex].notes[index].durationBeats)))
    throw new Error('Track MIDI and full-song MIDI revision differ');

  const id = randomUUID(), destination = path.join(jobsRoot, id);
  mkdirSync(destination, { recursive: false });
  for (const filename of ['composition-plan.json', 'phrase-architecture.json',
    'full-song.pulso.json', ...manifest.tracks.flatMap(part =>
      [part.filename, part.filename.replace(/\.mid$/i, '.pulso.json')])])
    if (existsSync(path.join(directory, filename)))
      copyFileSync(path.join(directory, filename), path.join(destination, filename));
  writeFileSync(path.join(destination, 'full-song.mid'), candidateBytes);
  writeFileSync(path.join(destination, trackFilename), isolated);
  writeFileSync(path.join(destination, 'manifest.json'), JSON.stringify({ ...manifest,
    title: `${manifest.title} · Articulation Revision`,
    articulationReview: { sourceJobId: sourceId, revisedTrack: track.name,
      changedNotes: edits.length, modelResponseId: responseId },
    editorial: { ...manifest.editorial, creative_ready: false,
      musical_review_required: true } }, null, 2));
  const sourceJob = readJson(path.join(directory, 'job.json'));
  writeFileSync(path.join(destination, 'job.json'), JSON.stringify({ ...sourceJob,
    created_at: new Date().toISOString(), experiment: 'ai-articulation-review-ab',
    review_from_job: sourceId }, null, 2));
  writeFileSync(path.join(destination, 'local-origin.json'), JSON.stringify({ origin: 'auto' }));
  writeFileSync(path.join(destination, 'progress.json'), JSON.stringify({
    at: new Date().toISOString(), state: 'completed', stage: 'ready', completed: 1,
    total: 1, attempt: 0, detail: 'AI articulation A/B review',
  }));
  const report = { originalJobId: sourceId, revisionJobId: id, title: manifest.title,
    lead: track.name, originalNoteCount: track.notes.length,
    revisionNoteCount: candidate.tracks[leadIndex].notes.length,
    summary: answer.summary, edits: edits.map(edit => ({
      bar: +(1 + edit.beat / score.beatsPerBar).toFixed(3),
      pitch: edit.pitch, before: edit.before, after: edit.durationBeats,
      reason: edit.reason })), usage, responseId };
  writeFileSync(path.join(destination, 'articulation-review-report.json'),
    JSON.stringify(report, null, 2));
  process.stdout.write(JSON.stringify(report, null, 2) + '\n');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
  main().catch(error => { process.stderr.write(`${error.message}\n`); process.exitCode = 1; });
