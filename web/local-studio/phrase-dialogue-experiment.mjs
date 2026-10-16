// Local, opt-in A/B experiment. The model rewrites two complete MIDI phrases;
// this file never invents, quantizes or deletes individual notes on its own.
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { randomUUID } from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseMidi, singleTrackMidi } from '../shared/midi.mjs';
import { jobDirectory, jobsRoot } from './storage.mjs';

const option = name => process.argv.find(value => value.startsWith(`--${name}=`))?.slice(name.length + 3);
const sourceJobId = option('source') || '61977e2a-97c9-46ea-964f-3e9f8e7088c3';
const firstBar = Number(option('bar') || 40);
const bars = Number(option('bars') || 8);
const mode = option('mode') || 'collision';
const firstBeat = (firstBar - 1) * 4;
const lastBeat = firstBeat + bars * 4;
const owners = [option('lead') || 'PULSO First Violin',
  option('answer') || 'PULSO Oboe'];

export function closeNeighborPairs(notes) {
  const sorted = [...notes].sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch);
  let count = 0;
  for (let i = 1; i < sorted.length; i++) {
    const gap = sorted[i].startBeat - sorted[i - 1].startBeat;
    if (gap > 0 && gap <= 0.5 && Math.abs(sorted[i].pitch - sorted[i - 1].pitch) <= 2) count++;
  }
  return count;
}

export function validatePhrase(notes, owner, start = firstBeat, end = lastBeat,
    range = owner === owners[0] ? [55, 96] : [58, 88]) {
  if (!Array.isArray(notes) || notes.length < 5 || notes.length > 128)
    throw new Error(`${owner}: phrase length invalid`);
  const converted = notes.map(note => {
    const { beat, duration, pitch, velocity } = note;
    if (![beat, duration, pitch, velocity].every(Number.isFinite) ||
        !Number.isInteger(pitch) || !Number.isInteger(velocity) ||
        pitch < range[0] || pitch > range[1] || velocity < 1 || velocity > 127 ||
        beat < 0 || duration < 0.25 || beat + duration > end - start + 0.0001 ||
        Math.abs(beat * 4 - Math.round(beat * 4)) > 0.0001 ||
        Math.abs(duration * 4 - Math.round(duration * 4)) > 0.0001)
      throw new Error(`${owner}: invalid MIDI event`);
    return { startBeat: start + beat, durationBeats: duration, pitch, velocity };
  });
  converted.sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch);
  for (let i = 0; i < converted.length; i++)
    for (let j = i + 1; j < converted.length && converted[j].startBeat <
         converted[i].startBeat + converted[i].durationBeats - 0.0001; j++)
      if (converted[i].pitch === converted[j].pitch)
        throw new Error(`${owner}: overlapping same-pitch attacks`);
  return converted;
}

export function overlappingDialogueBars(first, second, start = firstBeat, barsCount = bars) {
  const sounding = (notes, left, right) => notes.some(note =>
    note.startBeat < right && note.startBeat + note.durationBeats > left);
  return Array.from({ length: barsCount }, (_, bar) => bar).filter(bar => {
    const left = start + bar * 4;
    return sounding(first, left, left + 4) && sounding(second, left, left + 4);
  });
}

export function sustainedSemitoneClashes(first, second, start, end) {
  const clashes = [];
  for (const a of first) for (const b of second) {
    const overlap = Math.min(a.startBeat + a.durationBeats,
      b.startBeat + b.durationBeats, end) - Math.max(a.startBeat, b.startBeat, start);
    if (Math.abs(a.pitch - b.pitch) === 1 && overlap >= 0.25 - 0.0001)
      clashes.push({ beat: Math.max(a.startBeat, b.startBeat, start),
        pitches: [a.pitch, b.pitch] });
  }
  return clashes;
}

function noteLedger(track, start, end) {
  return track.notes.filter(note => note.startBeat < end &&
    note.startBeat + note.durationBeats > start).map(note => [
    Number((note.startBeat - firstBeat).toFixed(2)), note.pitch,
    Number(note.durationBeats.toFixed(2)), note.velocity]);
}

function outputText(response) {
  return response.output?.flatMap(item => item.content || [])
    .filter(item => item.type === 'output_text').map(item => item.text).join('') || '';
}

async function openAiPhrase(prompt) {
  const key = process.env.OPENAI_API_KEY;
  if (!key) throw new Error('OPENAI_API_KEY is not configured');
  const schemaNote = { type: 'object', properties: {
    beat: { type: 'number' }, duration: { type: 'number' },
    pitch: { type: 'integer' }, velocity: { type: 'integer' },
  }, required: ['beat', 'duration', 'pitch', 'velocity'], additionalProperties: false };
  const body = {
    model: 'gpt-5.6-terra', background: true,
    reasoning: { effort: 'medium' }, max_output_tokens: mode === 'preserve' ? 11000 : 6500,
    input: prompt,
    text: { format: { type: 'json_schema', name: 'pulso_phrase_dialogue_ab',
      strict: true, schema: { type: 'object', properties: {
        intent: { type: 'string' },
        lead_notes: { type: 'array', items: schemaNote },
        answer_notes: { type: 'array', items: schemaNote },
      }, required: ['intent', 'lead_notes', 'answer_notes'], additionalProperties: false } } },
  };
  const headers = { Authorization: `Bearer ${key}`, 'Content-Type': 'application/json' };
  let response = await fetch('https://api.openai.com/v1/responses', {
    method: 'POST', headers, body: JSON.stringify(body), signal: AbortSignal.timeout(30000),
  });
  let data = await response.json();
  if (!response.ok) throw new Error(`OpenAI request failed (${response.status}): ${data.error?.code || data.error?.type || 'unknown'}`);
  const deadline = Date.now() + 180000;
  while (['queued', 'in_progress'].includes(data.status) && Date.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, 4000));
    response = await fetch(`https://api.openai.com/v1/responses/${encodeURIComponent(data.id)}`, {
      headers: { Authorization: `Bearer ${key}` }, signal: AbortSignal.timeout(30000),
    });
    data = await response.json();
    if (!response.ok) throw new Error(`OpenAI poll failed (${response.status})`);
  }
  if (data.status !== 'completed') throw new Error(`AI phrase response ${data.status}; original unchanged`);
  const text = outputText(data);
  if (!text) throw new Error('AI phrase response empty; original unchanged');
  return { draft: JSON.parse(text), usage: data.usage || null, responseId: data.id };
}

const u32 = n => [(n >>> 24) & 255, (n >>> 16) & 255, (n >>> 8) & 255, n & 255];
function vlq(number) {
  if (!Number.isInteger(number) || number < 0 || number > 0x0fffffff)
    throw new Error('Invalid MIDI delta');
  const bytes = [number & 127];
  while ((number >>>= 7)) bytes.unshift((number & 127) | 128);
  return bytes;
}

function replacementChunk(track, division, songLength) {
  const events = [];
  const name = new TextEncoder().encode(track.name);
  events.push({ tick: 0, order: 0, bytes: [0xff, 0x03, ...vlq(name.length), ...name] });
  for (const note of track.notes) {
    const channel = note.channel ?? 0;
    const start = Math.round(note.startBeat * division);
    const end = Math.max(start + 1, Math.round((note.startBeat + note.durationBeats) * division));
    events.push({ tick: start, order: 2, bytes: [0x90 | channel, note.pitch, note.velocity] });
    events.push({ tick: end, order: 1, bytes: [0x80 | channel, note.pitch, 0] });
  }
  events.sort((a, b) => a.tick - b.tick || a.order - b.order);
  const payload = [];
  let previous = 0;
  for (const event of events) {
    payload.push(...vlq(event.tick - previous), ...event.bytes);
    previous = event.tick;
  }
  payload.push(...vlq(Math.max(previous, Math.round(songLength * division)) - previous), 0xff, 0x2f, 0);
  return Uint8Array.from([0x4d, 0x54, 0x72, 0x6b, ...u32(payload.length), ...payload]);
}

export function replaceTracks(originalBytes, score, replacements) {
  const bytes = new Uint8Array(originalBytes);
  const view = new DataView(bytes.buffer, bytes.byteOffset, bytes.byteLength);
  const headerLength = view.getUint32(4);
  const headerEnd = 8 + headerLength;
  if (headerEnd > bytes.length || score.tracks.length !== view.getUint16(10))
    throw new Error('Invalid source MIDI header');
  const chunks = [bytes.slice(0, headerEnd)];
  let offset = headerEnd;
  for (let i = 0; i < score.tracks.length; i++) {
    if (String.fromCharCode(...bytes.slice(offset, offset + 4)) !== 'MTrk')
      throw new Error('Invalid source MIDI track');
    const length = view.getUint32(offset + 4);
    const end = offset + 8 + length;
    if (end > bytes.length) throw new Error('Truncated source MIDI track');
    chunks.push(replacements.has(i) ? replacementChunk(score.tracks[i], score.division,
      score.lengthBeats) : bytes.slice(offset, end));
    offset = end;
  }
  const result = new Uint8Array(chunks.reduce((sum, chunk) => sum + chunk.length, 0));
  let index = 0;
  for (const chunk of chunks) { result.set(chunk, index); index += chunk.length; }
  return result;
}

async function main() {
  const sourceDirectory = jobDirectory(sourceJobId);
  if (!existsSync(path.join(sourceDirectory, 'full-song.mid')))
    throw new Error('Reference composition is missing');
  const originalBytes = readFileSync(path.join(sourceDirectory, 'full-song.mid'));
  const score = parseMidi(originalBytes);
  const plan = JSON.parse(readFileSync(path.join(sourceDirectory, 'composition-plan.json'), 'utf8'));
  const original = new Map(owners.map(name => [name,
    score.tracks.find(track => track.name === name)]));
  if ([...original.values()].some(track => !track)) throw new Error('Expected voices missing');
  const beforeNotes = new Map(owners.map(name => [name,
    original.get(name).notes.filter(note => note.startBeat >= firstBeat &&
      note.startBeat < lastBeat).map(note => ({ ...note }))]));
  const answerEntry = original.get(owners[1]).notes.find(note =>
    note.startBeat >= firstBeat && note.startBeat < lastBeat)?.startBeat;
  const answerSectionEntry = Number.isFinite(answerEntry) ?
    Math.max(firstBeat, Math.floor(answerEntry / 4) * 4) : undefined;
  const harmony = plan.sections.flatMap(section => section.harmonic_events || [])
    .filter(event => event.absolute_beat >= firstBeat - 8 && event.absolute_beat < lastBeat + 8)
    .map(event => ({ beat: event.absolute_beat - firstBeat, chord: event.chord_id,
      purpose: event.purpose }));
  const phrasePlan = existsSync(path.join(sourceDirectory, 'phrase-architecture.json')) ?
    JSON.parse(readFileSync(path.join(sourceDirectory, 'phrase-architecture.json'), 'utf8')) : null;
  const relevantPhrases = phrasePlan?.phrases?.filter(phrase => {
    const section = plan.sections[phrase.section_index];
    if (!section) return false;
    const start = section.start_bar + phrase.local_start_bar;
    return start < firstBar + bars && start + phrase.bars >= firstBar;
  }) || [];
  const preserveDirection = [
    `Rewrite ONLY ${owners[0]} and ${owners[1]} in bars ${firstBar}-${firstBar + bars - 1} of this ${plan.total_bars}-bar piece.`,
    'This A/B tests melodic authorship and the relation between phrases, NOT fewer notes.',
    'Preserve the original recognisable rhythmic gesture and its useful quick runs; the listener preferred those to an over-thinned prior edit.',
    'Do not turn the lead into long dry isolated notes. Keep its momentum and the most distinctive attacks, but make each phrase have a premise, continuation, consequence and audible arrival or deliberate deferral.',
    'When a fast figure is repeated, let the next statement change its destination or stress for a musical reason; do not just copy the three notes again.',
    'In this reference, the climax lead often repeats the same short rising four-note contour on successive chord roots. Keep one unmistakable statement, then develop its consequence across several bars instead of transposing the whole figure every bar.',
    'The answerer mostly plays tiny late-bar bursts. Give it at least one recognizable multi-bar answer with a beginning, middle and destination; quick pickups may stay, but they must lead somewhere.',
    `The answerer is deliberately absent before the climax section at beat ${answerSectionEntry}; do not fill earlier sections, but choose its entrance freely within the climax.`,
    'The climax should feel like one larger melodic arc through Dm, Eb, Gm and A tension, not a row of unrelated one-bar riffs.',
    'Keep the rhythmic signature recognisable at the climax entrance, but recompose the middle and final return as one long musical sentence. At the end, make the changed phrase actually arrive rather than repeating the same transposed four-note figure.',
    'A rewrite that retains nearly every original pitch-and-onset pair is not a new interpretation. Preserve motif IDENTITY, not the literal note list.',
    'The answer voice can overlap, interrupt, or stay silent where musically necessary. Do NOT force constant contrapuntal overlap.',
    'The answerer should refer to an actual lead gesture while remaining an independent line, not a duplicate or chord-tone filler.',
    'The protagonist MUST speak first at the climax downbeat and remain the principal narrator. Do not transfer its four-note melody into the answerer or reduce the protagonist to occasional late-bar fills.',
    'The answerer enters after the lead opening statement, at most roughly as active as the original answer lane; it should articulate a few consequential replies, not play the lead riff on another track in every bar.',
    'Keep a plausible amount of notes and variety of note lengths; removing half the melody to improve a mechanical metric is not allowed.',
    `This exact lead has ${beforeNotes.get(owners[0]).length} notes in the selected span. Preserve its comparable melodic activity (roughly 32-48 authored attacks), not a sparse sketch. Keep characteristic short pickups and change the CONSEQUENCE of recurring phrases.`,
    'No deterministic note deletion, no new percussion, no new instruments. Author every replacement note.',
    `GLOBAL MOTIF ${plan.motif_identity || ''}; HARMONIC DEBT ${plan.harmonic_debt || ''}`,
    `RELEVANT PHRASE PLAN ${JSON.stringify(relevantPhrases)}`,
    `OPENING LEAD MOTIF ${JSON.stringify(noteLedger(original.get(owners[0]), 0, 32))}`,
    `LEAD BEFORE WINDOW ${JSON.stringify(noteLedger(original.get(owners[0]), Math.max(0, firstBeat - 16), firstBeat))}`,
    `LEAD AFTER WINDOW ${JSON.stringify(noteLedger(original.get(owners[0]), lastBeat, lastBeat + 16))}`,
  ];
  const collisionDirection = [
    'Rewrite ONLY the first violin and oboe in bars 40-47 of this 152-bar piece.',
    'Compose two musically independent, complementary voices: a violin question and an oboe answer.',
    'This is COUNTERPOINT, not a timbral handoff. Both voices must sound within at least 4 of the 8 bars, including bar 43 (relative bar 3).',
    'The oboe must enter before bar 42 and the violin must continue after bar 44. They may breathe, but neither may disappear for half the passage.',
    'The oboe must have its own melodic consequences, not just repeat chord tones or shadow the violin.',
    'The listener complained that tightly repeated or neighbouring notes can sound like robotic mi-mi-mi-mi.',
    'Keep memorable rhythmic identity where it matters, but shape attacks into phrases with breathing, held destinations and varied consequences.',
    'The ORIGINAL violin contains ten very-close near-neighbour pairs in eight bars. Redesign, do not copy, its literal fast A-G-A / D-E-D and similar three-attack curls.',
    'The edit should audibly reduce those tight runs in BOTH voices together; prefer a few expressive quick figures separated by genuinely longer notes or rests.',
    'Do not merely change velocities or orchestration. Change the note-on rhythm and melodic direction where the repetitive figure sounds mechanical.',
    'Do not create a constant stream of equally spaced stepwise notes or artificially add note density.',
    'The Dm -> D7 -> G harmonic turn must stay meaningful; F natural over Dm and F sharp over D7 serve different functions.',
    'Critical listening issue at BAR 43 (relative beats 12-16): do not sustain E5 (MIDI 76) against F5 (MIDI 77) or any simultaneous adjacent semitone between these two voices there. Preserve a clear D-minor reading.',
  ];
  const prompt = [
    'You are revising a real, accepted MIDI composition, not inventing a new piece.',
    ...(mode === 'preserve' ? preserveDirection : collisionDirection),
    'Preserve the broad dramatic role and recognizable melodic argument. Other instruments are immutable.',
    `Return a COMPLETE replacement for each of these two voices over ${bars} bars, not a list of edits.`,
    `beats are relative to the beginning of bar ${firstBar}: 0 <= beat < ${bars * 4}; each duration ends by beat ${bars * 4}.`,
    'Use exact 0.25-beat grid for attacks and durations. No overlapping same-pitch notes.',
    `Pitches are MIDI integers; velocities 1-127. JSON lead_notes belongs ONLY to ${owners[0]}, and answer_notes belongs ONLY to ${owners[1]}.`,
    `HARMONY ${JSON.stringify(harmony)}`,
    `LEAD ${owners[0]} ORIGINAL [relativeBeat,pitch,duration,velocity] ${JSON.stringify(noteLedger(original.get(owners[0]), firstBeat, lastBeat))}`,
    `ANSWER ${owners[1]} ORIGINAL [relativeBeat,pitch,duration,velocity] ${JSON.stringify(noteLedger(original.get(owners[1]), firstBeat, lastBeat))}`,
    ...score.tracks.filter(track => !owners.includes(track.name) && track.notes.some(note =>
      note.startBeat >= firstBeat && note.startBeat < lastBeat)).map(track =>
      `FIXED ${track.name} ${JSON.stringify(noteLedger(track, firstBeat, lastBeat))}`),
  ].join('\n');
  const { draft, usage, responseId } = await openAiPhrase(prompt);
  const rangeFor = name => {
    const pitches = original.get(name).notes.map(note => note.pitch);
    return [Math.max(0, Math.min(...pitches) - 12),
      Math.min(127, Math.max(...pitches) + 12)];
  };
  const replacements = new Map([
    [owners[0], validatePhrase(draft.lead_notes, owners[0], firstBeat, lastBeat,
      rangeFor(owners[0]))],
    [owners[1], validatePhrase(draft.answer_notes, owners[1], firstBeat, lastBeat,
      rangeFor(owners[1]))],
  ]);
  const dialogueBars = overlappingDialogueBars(replacements.get(owners[0]),
    replacements.get(owners[1]));
  if (mode !== 'preserve' && (dialogueBars.length < 4 || !dialogueBars.includes(3) ||
      !replacements.get(owners[1]).some(note => note.startBeat < firstBeat + 8) ||
      !replacements.get(owners[0]).some(note => note.startBeat >= firstBeat + 20)))
    throw new Error('AI produced a handoff rather than counterpoint; original unchanged');
  if (mode === 'preserve' && (dialogueBars.length < 2 ||
      replacements.get(owners[0]).length < beforeNotes.get(owners[0]).length * 0.65))
    throw new Error(`AI thinned melodic identity: lead ${beforeNotes.get(owners[0]).length}->${replacements.get(owners[0]).length}, dialogue bars ${dialogueBars.length}; original unchanged`);
  if (mode === 'preserve' && (
      replacements.get(owners[0])[0]?.startBeat > firstBeat + 1.0001 ||
      replacements.get(owners[1])[0]?.startBeat < firstBeat + 1.0 - 0.0001 ||
      replacements.get(owners[1]).length > Math.ceil(beforeNotes.get(owners[1]).length * 1.35) ||
      replacements.get(owners[0]).length < replacements.get(owners[1]).length * 0.9))
    throw new Error(`AI moved the lead role into the answerer: lead ${replacements.get(owners[0]).length}, answer ${replacements.get(owners[1]).length}; original unchanged`);
  if (mode === 'preserve') {
    const originals = new Set(beforeNotes.get(owners[0]).map(note =>
      `${note.startBeat}:${note.pitch}`));
    const identical = replacements.get(owners[0]).filter(note =>
      originals.has(`${note.startBeat}:${note.pitch}`)).length;
    if (identical > Math.ceil(beforeNotes.get(owners[0]).length * 0.75))
      throw new Error(`AI copied ${identical}/${beforeNotes.get(owners[0]).length} lead attacks; original unchanged`);
  }
  if (mode === 'preserve' && Number.isFinite(answerSectionEntry) &&
      replacements.get(owners[1]).some(note =>
        note.startBeat < answerSectionEntry - 0.0001))
    throw new Error('AI moved the answer voice before its planned climax entry; original unchanged');
  const quickViolin = closeNeighborPairs(replacements.get(owners[0]));
  const quickTotal = quickViolin + closeNeighborPairs(replacements.get(owners[1]));
  if (mode !== 'preserve' && (quickViolin > 6 || quickTotal > 8))
    throw new Error('AI retained the mechanical near-neighbour runs; original unchanged');
  if (mode !== 'preserve' && sustainedSemitoneClashes(replacements.get(owners[0]),
      replacements.get(owners[1]), firstBeat + 12, firstBeat + 16).length)
    throw new Error('AI created an adjacent sustained clash in bar 43; original unchanged');
  const replacedIndices = new Set();
  for (let i = 0; i < score.tracks.length; i++) {
    const track = score.tracks[i];
    if (!replacements.has(track.name)) continue;
    const channel = track.notes[0]?.channel ?? 0;
    track.notes = [
      ...track.notes.filter(note => note.startBeat < firstBeat || note.startBeat >= lastBeat),
      ...replacements.get(track.name).map(note => ({ ...note, channel })),
    ].sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch);
    replacedIndices.add(i);
  }
  const editedBytes = replaceTracks(originalBytes, score, replacedIndices);
  const parsed = parseMidi(editedBytes);
  if (parsed.tracks.length !== score.tracks.length || parsed.tracks.some((track, i) =>
    track.notes.length !== score.tracks[i].notes.length))
    throw new Error('Edited MIDI failed round-trip validation');
  const id = randomUUID();
  const destination = path.join(jobsRoot, id);
  mkdirSync(destination, { recursive: true });
  const manifest = JSON.parse(readFileSync(path.join(sourceDirectory, 'manifest.json'), 'utf8'));
  manifest.title = `${manifest.title} - Dialogo A/B`;
  manifest.comparisonFile = 'reference-standard.mid';
  manifest.comparisonNotes = parseMidi(originalBytes).tracks.reduce((sum, t) => sum + t.notes.length, 0);
  manifest.all_midi_notes = parsed.tracks.reduce((sum, t) => sum + t.notes.length, 0);
  manifest.ai_authored_or_declared_transform_notes = manifest.all_midi_notes;
  manifest.note_provenance = { direct_ai: manifest.all_midi_notes, reused_ai: 0,
    transformed_ai: 0, reused_transformed_ai: 0, other: 0 };
  manifest.experimental_phrase_dialogue = { source_job: sourceJobId, first_bar: firstBar,
    bars, voices: owners, intent: draft.intent, response_id: responseId,
    note_only_midi_export: true };
  // The source editorial scores describe the unedited performance, so do not
  // advertise them as a fresh verdict on this A/B candidate.
  manifest.editorial = { technical_ready: true, musical_review_required: true,
    experimental_comparison_only: true };
  for (let i = 0; i < manifest.tracks.length; i++) {
    const entry = manifest.tracks[i];
    entry.notes = parsed.tracks[i + 1]?.notes.length ?? entry.notes;
    const filename = entry.filename;
    const sourceTrack = path.join(sourceDirectory, filename);
    if (replacedIndices.has(i + 1))
      writeFileSync(path.join(destination, filename), singleTrackMidi(score, [score.tracks[i + 1]]));
    else copyFileSync(sourceTrack, path.join(destination, filename));
    const sidecar = filename.replace(/\.mid$/, '.pulso.json');
    if (existsSync(path.join(sourceDirectory, sidecar))) {
      const originalSidecar = JSON.parse(readFileSync(path.join(sourceDirectory, sidecar), 'utf8'));
      if (replacedIndices.has(i + 1)) {
        originalSidecar.experimental_note_edit = true;
        for (const part of originalSidecar.parts || [])
          part.note_count = entry.notes;
        writeFileSync(path.join(destination, sidecar),
          JSON.stringify(originalSidecar, null, 2));
      } else copyFileSync(path.join(sourceDirectory, sidecar),
        path.join(destination, sidecar));
    }
  }
  for (const filename of ['composition-plan.json', 'phrase-architecture.json'])
    if (existsSync(path.join(sourceDirectory, filename)))
      copyFileSync(path.join(sourceDirectory, filename), path.join(destination, filename));
  writeFileSync(path.join(destination, 'reference-standard.mid'), originalBytes);
  writeFileSync(path.join(destination, 'full-song.mid'), editedBytes);
  writeFileSync(path.join(destination, 'manifest.json'), JSON.stringify(manifest, null, 2));
  const sourceJob = JSON.parse(readFileSync(path.join(sourceDirectory, 'job.json'), 'utf8'));
  writeFileSync(path.join(destination, 'job.json'), JSON.stringify({ ...sourceJob,
    created_at: new Date().toISOString(), review_from_job: sourceJobId,
    experiment: 'phrase-dialogue-ab' }, null, 2));
  writeFileSync(path.join(destination, 'local-origin.json'), JSON.stringify({ origin: 'auto' }));
  writeFileSync(path.join(destination, 'progress.json'), JSON.stringify({
    at: new Date().toISOString(), state: 'completed', stage: 'ready', completed: 1,
    total: 1, attempt: 0, detail: 'Local AI phrase-dialogue A/B experiment',
  }));
  const report = { source: sourceJobId, candidate: id, bars: [firstBar, firstBar + bars - 1],
    intent: draft.intent, usage, overlappingDialogueBars: dialogueBars.map(bar => firstBar + bar),
    voices: owners.map(name => {
      const before = beforeNotes.get(name);
      const after = replacements.get(name);
      return { name, beforeNotes: before.length, afterNotes: after.length,
        closeNeighborPairsBefore: closeNeighborPairs(before),
        closeNeighborPairsAfter: closeNeighborPairs(after) };
    }) };
  writeFileSync(path.join(destination, 'phrase-dialogue-report.json'), JSON.stringify(report, null, 2));
  process.stdout.write(JSON.stringify({ job: id, report }, null, 2) + '\n');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
  main().catch(error => { process.stderr.write(`${error.message}\n`); process.exitCode = 1; });
