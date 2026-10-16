// Opt-in local experiment: an AI editor rewrites complete foreground phrases.
// Validators and measurements never invent, move, quantize, or delete notes.
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { createHash, randomUUID } from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseMidi, singleTrackMidi } from '../shared/midi.mjs';
import { jobDirectory, jobsRoot } from './storage.mjs';
import { replaceTracks } from './phrase-dialogue-experiment.mjs';

const option = name => process.argv.find(arg => arg.startsWith(`--${name}=`))?.slice(name.length + 3);
const sourceId = option('source') || '4a9102c8-f186-45fb-b497-bbbe47815b2a';
const leadName = option('lead') || 'PULSO Violín I protagonista';
const answerName = option('answer') || 'PULSO Clarinete respondedor';
const sectionsPerPass = [[0, 2], [2, 4], [4, 5]];
const noteSchema = { type: 'object', properties: {
  beat: { type: 'number' }, duration: { type: 'number' },
  pitch: { type: 'integer' }, velocity: { type: 'integer' },
}, required: ['beat', 'duration', 'pitch', 'velocity'], additionalProperties: false };
const writerSchema = { type: 'object', properties: {
  musical_intent: { type: 'string' },
  lead: { type: 'array', items: noteSchema },
  answer: { type: 'array', items: noteSchema },
}, required: ['musical_intent', 'lead', 'answer'], additionalProperties: false };
const criticSchema = { type: 'object', properties: {
  preferred: { type: 'string', enum: ['A', 'B', 'tie'] },
  narrative_reason: { type: 'string' },
  motif_reason: { type: 'string' },
  dialogue_reason: { type: 'string' },
  harmony_reason: { type: 'string' },
  closure_reason: { type: 'string' },
  remaining_issues: { type: 'array', items: { type: 'string' } },
}, required: ['preferred', 'narrative_reason', 'motif_reason', 'dialogue_reason',
  'harmony_reason', 'closure_reason', 'remaining_issues'], additionalProperties: false };

const sleep = ms => new Promise(resolve => setTimeout(resolve, ms));
const responseText = response => response.output?.flatMap(item => item.content || [])
  .filter(item => item.type === 'output_text').map(item => item.text).join('') || '';
const ledger = notes => notes.map(note => [note.startBeat, note.pitch,
  note.durationBeats, note.velocity]);

async function askModel(prompt, name, schema, maxOutputTokens) {
  const key = process.env.OPENAI_API_KEY;
  if (!key) throw new Error('OPENAI_API_KEY is not configured');
  const headers = { Authorization: `Bearer ${key}`, 'Content-Type': 'application/json' };
  const body = { model: 'gpt-5.6-terra', background: true,
    reasoning: { effort: 'medium' }, max_output_tokens: maxOutputTokens,
    input: prompt,
    text: { format: { type: 'json_schema', name, strict: true, schema } } };
  let reply = await fetch('https://api.openai.com/v1/responses', {
    method: 'POST', headers, body: JSON.stringify(body), signal: AbortSignal.timeout(30000),
  });
  let response = await reply.json();
  if (!reply.ok) throw new Error(`AI request failed (${reply.status}): ${response.error?.code || 'unknown'}`);
  const deadline = Date.now() + 360000;
  while (['queued', 'in_progress'].includes(response.status) && Date.now() < deadline) {
    await sleep(5000);
    reply = await fetch(`https://api.openai.com/v1/responses/${encodeURIComponent(response.id)}`,
      { headers, signal: AbortSignal.timeout(30000) });
    response = await reply.json();
    if (!reply.ok) throw new Error(`AI polling failed (${reply.status})`);
  }
  if (['queued', 'in_progress'].includes(response.status)) {
    await fetch(`https://api.openai.com/v1/responses/${encodeURIComponent(response.id)}/cancel`,
      { method: 'POST', headers, signal: AbortSignal.timeout(30000) }).catch(() => {});
  }
  if (response.status !== 'completed')
    throw new Error(`AI response ${response.status}; source retained`);
  const output = responseText(response);
  if (!output) throw new Error('Empty AI response; source retained');
  return { value: JSON.parse(output), usage: response.usage || null, responseId: response.id };
}

export function validateAuthoredVoice(events, name, fromBeat, toBeat, range) {
  if (!Array.isArray(events) || events.length > 120)
    throw new Error(`${name}: invalid event list`);
  const notes = events.map(event => {
    const { beat, duration, pitch, velocity } = event;
    if (![beat, duration, pitch, velocity].every(Number.isFinite) ||
        !Number.isInteger(pitch) || pitch < range[0] || pitch > range[1] ||
        !Number.isInteger(velocity) || velocity < 1 || velocity > 127 ||
        beat < fromBeat || beat >= toBeat || duration < 0.25 ||
        beat + duration > toBeat + 0.0001 ||
        Math.abs(beat * 4 - Math.round(beat * 4)) > 0.0001 ||
        Math.abs(duration * 4 - Math.round(duration * 4)) > 0.0001)
      throw new Error(`${name}: invalid authored note`);
    return { startBeat: beat, durationBeats: duration, pitch, velocity };
  }).sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch);
  for (let i = 1; i < notes.length; i++)
    if (notes[i].startBeat < notes[i - 1].startBeat + notes[i - 1].durationBeats - 0.0001)
      throw new Error(`${name}: overlapping monophonic notes`);
  return notes;
}

export function phraseResponseGaps(plan, phraseAudit, notes, answerId) {
  return phraseAudit.phrases.filter(phrase => phrase.responseId === answerId)
    .filter(phrase => {
      const from = phrase.absoluteStartBar * (plan.beats_per_bar || 4);
      const to = from + phrase.bars * (plan.beats_per_bar || 4);
      return !notes.some(note => note.startBeat >= from && note.startBeat < to);
    }).length;
}

export function repeatedFourNoteCells(notes) {
  const counts = new Map();
  for (let i = 0; i < notes.length - 3; i++) {
    const group = notes.slice(i, i + 4);
    if (group.some((note, index) => index > 0 &&
        note.startBeat - group[index - 1].startBeat > 3)) continue;
    const signature = group.slice(1).map((note, index) => [
      Math.round((note.startBeat - group[index].startBeat) * 4),
      note.pitch - group[index].pitch,
      Math.round(note.durationBeats * 4),
    ].join(':')).join('|');
    counts.set(signature, (counts.get(signature) || 0) + 1);
  }
  return Math.max(0, ...counts.values());
}

export function sustainedSemitoneAgainstHarmony(lead, piano, from, to) {
  let count = 0;
  for (const note of lead.filter(note => note.startBeat >= from && note.startBeat < to))
    for (const chordNote of piano) {
      const overlap = Math.min(note.startBeat + note.durationBeats,
        chordNote.startBeat + chordNote.durationBeats, to) -
        Math.max(note.startBeat, chordNote.startBeat, from);
      const pitchClassDistance = Math.abs(note.pitch % 12 - chordNote.pitch % 12);
      if (overlap >= 0.5 && Math.min(pitchClassDistance, 12 - pitchClassDistance) === 1)
        count++;
    }
  return count;
}

function scenePrompt({ job, plan, phrasePlan, original, authored, leadIndex,
    answerIndex, firstSection, endSection, feedback = '' }) {
  const sections = plan.sections.slice(firstSection, endSection);
  const from = sections[0].start_bar * 4;
  const to = (sections.at(-1).start_bar + sections.at(-1).bars) * 4;
  const relevant = phrasePlan?.phrases?.filter(phrase =>
    phrase.section_index >= firstSection && phrase.section_index < endSection) || [];
  const answerWindows = relevant.filter(phrase =>
    phrase.response_id === 'clarinet_motif_answerer').map(phrase => ({
      from_bar: plan.sections[phrase.section_index].start_bar +
        phrase.local_start_bar + 1,
      through_bar: plan.sections[phrase.section_index].start_bar +
        phrase.local_start_bar + phrase.bars,
      speaker: phrase.speaker_id,
      consequence: phrase.consequence,
    }));
  const before = authored.lead.filter(note => note.startBeat >= Math.max(0, from - 16));
  const beforeAnswer = authored.answer.filter(note => note.startBeat >= Math.max(0, from - 16));
  const fixed = original.tracks.filter((track, index) =>
    index !== leadIndex && index !== answerIndex && track.notes.some(note =>
      note.startBeat >= from && note.startBeat < to));
  return [
    'You are the musical editor of a real original MIDI composition. Rewrite TWO COMPLETE independent foreground performances over this continuous scene. Every output note is your musical decision, not a list of machine edits. Keep all other tracks and harmonic events fixed. Preserve the identity of the opening question, the five-act dramatic form, and the concluding tonic. Compose in dialogue with the fixed ensemble, not as two unrelated solos.',
    'A listener hears a too-literal four-note figure repeated in the violin; the planned clarinet response misses some phrases; a chromatic peak can rub a sustained chord. Develop the motif through changing consequence, rhythmic placement, contour, phrase length, register and rests when musically motivated. Repetition is welcome when it earns a transformation. Make the clarinet answer a *specific* violin thought, not double it or mechanically fill every silence. Make the climax a consequential longer sentence, not merely louder and higher sequential copies. Keep foreground notes singable on a neutral piano.',
    'Development must NOT mean adding a great many extra attacks. Keep violin and clarinet activity comparable to the original in each scene, and make a few phrases longer or differently directed rather than filling the original breaths. The closing violin must audibly complete the opening question on C after the final V7-I, with space after that arrival. The clarinet should make a genuine answer inside each planned response window, then may fall silent elsewhere. These are relationships, not note quotas.',
    'Chromatic color is allowed only with convincing voice leading and resolution against sustained harmony. In the Neapolitan passage, pay particular attention to the actual fixed piano pitches and do not prolong an unmotivated semitone against them. Do not flatten all tension into simple triads. The return to C must feel earned, not tacked on.',
    `Write absolute quarter-note beats from ${from} inclusive to ${to} exclusive, 4/4, grid 0.25 beat; each note duration >= 0.25 and ends by ${to}. Return complete notes for the violin and clarinet in chronological order. MIDI pitch integer; velocity 1-127. Both voices are monophonic. Violin range 60-96; clarinet range 48-88. No drums, no new tracks, no notes outside this scene.`,
    `USER BRIEF ${JSON.stringify(job.prompt)}`,
    `MOTIF PREMISE ${plan.motif_identity}`,
    `HARMONIC DEBT ${plan.harmonic_debt}`,
    `RESOLUTION INTENT ${plan.resolution_intent}`,
    `SCENES ${JSON.stringify(sections.map(section => ({ name: section.name,
      start_bar: section.start_bar, bars: section.bars, function: section.function,
      motif_treatment: section.motif_treatment, harmonic_events: section.harmonic_events })))}`,
    `CHORD PALETTE ${JSON.stringify(plan.chord_palette.map(chord => ({ id: chord.id,
      pitch_classes: chord.pitch_classes, bass_pitch_class: chord.bass_pitch_class })))}`,
    `PHRASE INTENTS ${JSON.stringify(relevant)}`,
    `CLARINET RESPONSE WINDOWS ${JSON.stringify(answerWindows)}`,
    `ORIGINAL VIOLIN [beat,pitch,duration,velocity] ${JSON.stringify(ledger(
      original.tracks[leadIndex].notes.filter(note => note.startBeat >= from && note.startBeat < to)))}`,
    `ORIGINAL CLARINET [beat,pitch,duration,velocity] ${JSON.stringify(ledger(
      original.tracks[answerIndex].notes.filter(note => note.startBeat >= from && note.startBeat < to)))}`,
    `ALREADY AUTHORED VIOLIN LAST 4 BARS ${JSON.stringify(ledger(before))}`,
    `ALREADY AUTHORED CLARINET LAST 4 BARS ${JSON.stringify(ledger(beforeAnswer))}`,
    ...fixed.map(track => `FIXED ${track.name} [beat,pitch,duration,velocity] ${JSON.stringify(
      ledger(track.notes.filter(note => note.startBeat >= from && note.startBeat < to)))}`),
    feedback ? `PREVIOUS DRAFT REJECTED: ${feedback}. Rewrite BOTH complete scene voices. Do not return isolated patches.` : '',
  ].join('\n');
}

function criticPrompt(plan, original, candidate, leadIndex, answerIndex) {
  const fixed = original.tracks.filter((_, index) => index !== leadIndex && index !== answerIndex);
  const noteMap = tracks => ({ violin: ledger(tracks[leadIndex].notes),
    clarinet: ledger(tracks[answerIndex].notes) });
  return [
    'You are a skeptical independent music editor, comparing two complete symbolic MIDI foregrounds against the SAME unchanged accompaniment. Judge the ACTUAL notes, not the intention or source label. A and B are anonymized. Prefer the one that makes the question transform coherently, lets the clarinet answer identifiable thoughts, makes the chromatic climax harmonically credible, and earns the final repose. Do not reward merely more notes, fewer notes, grid correctness, or textbook consonance. If neither is clearly better, choose tie. Cite actual bar ranges and concrete musical evidence.',
    'If a phrase is copied as the same four-note figure transposed bar after bar, treat it as a weakness unless the harmonic consequences genuinely develop it. Check the Neapolitan passage against the fixed piano, and the final V7-I arrival. The score must be musical on a neutral piano.',
    `FORM ${JSON.stringify(plan.sections.map(section => ({ name: section.name,
      start_bar: section.start_bar, bars: section.bars, harmonic_events: section.harmonic_events })))}`,
    `MOTIF PREMISE ${plan.motif_identity}`,
    ...fixed.map(track => `UNCHANGED ${track.name} ${JSON.stringify(ledger(track.notes))}`),
    `A ${JSON.stringify(noteMap(original.tracks))}`,
    `B ${JSON.stringify(noteMap(candidate.tracks))}`,
  ].join('\n');
}

function measurements(score, plan, audit, leadIndex, answerIndex) {
  const lead = score.tracks[leadIndex].notes;
  const answer = score.tracks[answerIndex].notes;
  const piano = score.tracks.find(track => track.name.includes('Piano armónico'))?.notes || [];
  const chromatic = plan.sections.flatMap(section => section.harmonic_events || [])
    .find(event => event.chord_id === 'bII6');
  return { notes: [lead.length, answer.length],
    leadFourNoteMaxRepeat: repeatedFourNoteCells(lead),
    plannedClarinetResponseGaps: phraseResponseGaps(plan, audit, answer,
      'clarinet_motif_answerer'),
    neapolitanSemitoneContacts: chromatic ? sustainedSemitoneAgainstHarmony(
      lead, piano, chromatic.absolute_beat, chromatic.absolute_beat + 8) : 0,
    finalLeadTonic: lead.some(note => note.startBeat >= (plan.total_bars - 2) * 4 &&
      note.pitch % 12 === plan.root_pitch_class),
  };
}

function publish(source, sourceJob, sourceManifest, originalBytes, score, candidate,
    report) {
  const id = randomUUID();
  const destination = path.join(jobsRoot, id);
  mkdirSync(destination, { recursive: true });
  const changed = new Set([report.leadIndex, report.answerIndex]);
  const bytes = replaceTracks(originalBytes, candidate, changed);
  const exported = parseMidi(bytes);
  if (exported.tracks.length !== score.tracks.length ||
      exported.tracks.some((track, index) => track.notes.length !== candidate.tracks[index].notes.length))
    throw new Error('MIDI round-trip lost AI-authored notes');
  const manifest = structuredClone(sourceManifest);
  manifest.title = `${sourceManifest.title} - Revisión narrativa A/B`;
  manifest.comparisonFile = 'reference-standard.mid';
  manifest.comparisonNotes = score.tracks.reduce((sum, track) => sum + track.notes.length, 0);
  manifest.all_midi_notes = exported.tracks.reduce((sum, track) => sum + track.notes.length, 0);
  manifest.ai_authored_or_declared_transform_notes = manifest.all_midi_notes;
  const reusedAi = score.tracks.reduce((sum, track, index) =>
    sum + (changed.has(index) ? 0 : track.notes.length), 0);
  manifest.note_provenance = { direct_ai: manifest.all_midi_notes - reusedAi,
    reused_ai: reusedAi, transformed_ai: 0, reused_transformed_ai: 0, other: 0 };
  manifest.first_pass_audition = false;
  manifest.review_audition = true;
  delete manifest.musicalAuditFile;
  manifest.experimental_narrative_editor = { source_job: sourceId,
    edited_voices: [leadName, answerName],
    writer_response_ids: report.writerResponses.map(item => item.responseId),
    critic_response_id: report.critic.responseId,
    critic_preferred: report.critic.value.preferred };
  manifest.editorial = { technical_ready: true, musical_review_required: true,
    experimental_comparison_only: true };
  for (let index = 0; index < manifest.tracks.length; index++) {
    const entry = manifest.tracks[index];
    entry.notes = exported.tracks[index + 1].notes.length;
    const target = path.join(destination, entry.filename);
    if (changed.has(index + 1))
      writeFileSync(target, singleTrackMidi(candidate, [candidate.tracks[index + 1]]));
    else copyFileSync(path.join(source, entry.filename), target);
    const sidecar = entry.filename.replace(/\.mid$/, '.pulso.json');
    if (existsSync(path.join(source, sidecar)))
      copyFileSync(path.join(source, sidecar), path.join(destination, sidecar));
  }
  for (const filename of ['composition-plan.json', 'phrase-architecture.json'])
    if (existsSync(path.join(source, filename)))
      copyFileSync(path.join(source, filename), path.join(destination, filename));
  writeFileSync(path.join(destination, 'full-song.mid'), bytes);
  writeFileSync(path.join(destination, 'reference-standard.mid'), originalBytes);
  writeFileSync(path.join(destination, 'manifest.json'), JSON.stringify(manifest, null, 2));
  writeFileSync(path.join(destination, 'job.json'), JSON.stringify({ ...sourceJob,
    created_at: new Date().toISOString(), review_from_job: sourceId,
    first_pass_audition: false, review_audition: true,
    experiment: 'narrative-editor-ab' }, null, 2));
  writeFileSync(path.join(destination, 'local-origin.json'), JSON.stringify({ origin: 'auto' }));
  writeFileSync(path.join(destination, 'progress.json'), JSON.stringify({
    at: new Date().toISOString(), state: 'completed', stage: 'ready',
    completed: 1, total: 1, attempt: 0,
    detail: 'AI narrative A/B reviewed against its original',
  }));
  writeFileSync(path.join(destination, 'narrative-editor-report.json'),
    JSON.stringify({ ...report, source: sourceId, candidate: id }, null, 2));
  return id;
}

async function main() {
  const source = jobDirectory(sourceId);
  if (!source || !existsSync(path.join(source, 'full-song.mid')))
    throw new Error('Source composition not found');
  const originalBytes = readFileSync(path.join(source, 'full-song.mid'));
  const original = parseMidi(originalBytes);
  const plan = JSON.parse(readFileSync(path.join(source, 'composition-plan.json'), 'utf8'));
  const audit = JSON.parse(readFileSync(path.join(source, 'phrase-audit.json'), 'utf8'));
  const phrasePlan = JSON.parse(readFileSync(path.join(source, 'phrase-architecture.json'), 'utf8'));
  const job = JSON.parse(readFileSync(path.join(source, 'job.json'), 'utf8'));
  const manifest = JSON.parse(readFileSync(path.join(source, 'manifest.json'), 'utf8'));
  const leadIndex = original.tracks.findIndex(track => track.name === leadName);
  const answerIndex = original.tracks.findIndex(track => track.name === answerName);
  if (leadIndex < 0 || answerIndex < 0 || original.beatsPerBar !== 4 ||
      plan.total_bars !== 56 || plan.sections.length !== 5)
    throw new Error('Source does not match this controlled full-song experiment');
  const sourceHash = createHash('sha256').update(originalBytes).digest('hex');
  const draftDirectory = path.join(path.dirname(jobsRoot), 'narrative-editor-drafts');
  const draftPath = path.join(draftDirectory, `${sourceId}.json`);
  const saved = existsSync(draftPath) ?
    JSON.parse(readFileSync(draftPath, 'utf8')) : null;
  if (saved && saved.sourceHash !== sourceHash)
    throw new Error('Saved draft belongs to another source MIDI');
  const authored = saved?.authored || { lead: [], answer: [] };
  const writerResponses = saved?.writerResponses || [];
  if (saved) process.stdout.write('Resuming AI-authored draft for independent review\n');
  else for (const [firstSection, endSection] of sectionsPerPass) {
    const from = plan.sections[firstSection].start_bar * 4;
    const last = plan.sections[endSection - 1];
    const to = (last.start_bar + last.bars) * 4;
    process.stdout.write(`AI editor: bars ${from / 4 + 1}-${to / 4}\n`);
    const answer = await askModel(scenePrompt({ job, plan, phrasePlan,
      original, authored, leadIndex, answerIndex, firstSection, endSection }),
      'pulso_narrative_editor_scene', writerSchema, 11000);
    authored.lead.push(...validateAuthoredVoice(answer.value.lead, leadName,
      from, to, [60, 96]));
    authored.answer.push(...validateAuthoredVoice(answer.value.answer, answerName,
      from, to, [48, 88]));
    writerResponses.push({ fromBar: from / 4 + 1, toBar: to / 4,
      responseId: answer.responseId, usage: answer.usage,
      musicalIntent: answer.value.musical_intent });
  }
  if (authored.lead.length < 100 || authored.lead.length > 165 ||
      authored.answer.length < 30 || authored.answer.length > 85)
    throw new Error('AI edit is a sparse or excessive sketch; source retained');
  const candidate = { ...original,
    tracks: original.tracks.map(track => ({ ...track, notes: [...track.notes] })) };
  candidate.tracks[leadIndex].notes = authored.lead;
  candidate.tracks[answerIndex].notes = authored.answer;
  const before = measurements(original, plan, audit, leadIndex, answerIndex);
  const after = measurements(candidate, plan, audit, leadIndex, answerIndex);
  process.stdout.write(`Symbolic comparison ${JSON.stringify({ before, after })}\n`);
  if (after.leadFourNoteMaxRepeat >= before.leadFourNoteMaxRepeat ||
      after.plannedClarinetResponseGaps >= before.plannedClarinetResponseGaps ||
      after.neapolitanSemitoneContacts > before.neapolitanSemitoneContacts ||
      !after.finalLeadTonic)
    throw new Error('AI edit missed the targeted musical improvement; source retained');
  if (!saved) {
    mkdirSync(draftDirectory, { recursive: true });
    writeFileSync(draftPath, JSON.stringify({ sourceHash, authored,
      writerResponses }, null, 2));
    process.stdout.write('Validated AI draft checkpointed before critique\n');
  }
  process.stdout.write('Independent AI editor: comparing complete foregrounds\n');
  const critic = await askModel(criticPrompt(plan, original, candidate,
    leadIndex, answerIndex), 'pulso_narrative_ab_critic', criticSchema, 7000);
  if (critic.value.preferred !== 'B')
    throw new Error(`Independent editor did not prefer B (${critic.value.preferred}): ${critic.value.narrative_reason}`);
  const report = { leadIndex, answerIndex, before, after, writerResponses,
    critic };
  const id = publish(source, job, manifest, originalBytes, original,
    candidate, report);
  process.stdout.write(`Accepted candidate ${id}\n`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
  main().catch(error => { process.stderr.write(`${error.message}\n`); process.exitCode = 1; });
