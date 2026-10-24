// Local, controlled A/B: the model rewrites one chord; no procedural notes.
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { randomUUID } from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseMidi, singleTrackMidi } from '../shared/midi.mjs';
import { replaceTracks } from './phrase-dialogue-experiment.mjs';
import { jobDirectory, jobsRoot } from './storage.mjs';

const sourceId = process.argv.find(arg => arg.startsWith('--source='))?.slice(9) ||
  '1e8675dd-3ed5-49ef-b125-3b3b02277fd0';
const sourceDir = jobDirectory(sourceId);
const readJson = name => JSON.parse(readFileSync(path.join(sourceDir, name), 'utf8'));
const ledger = notes => notes.map(n => [n.startBeat, n.durationBeats, n.pitch, n.velocity]);
const schema = { type: 'object', additionalProperties: false, properties: {
  musical_intent: { type: 'string' },
  notes: { type: 'array', items: { type: 'object', additionalProperties: false,
    properties: { pitch: { type: 'integer' }, velocity: { type: 'integer' } },
    required: ['pitch', 'velocity'] } },
}, required: ['musical_intent', 'notes'] };

export function validateVoicing(authored, allowed, sourceCount) {
  if (!Array.isArray(authored) || !authored.length || authored.length >= sourceCount)
    throw new Error('AI did not simplify the 11-note chord');
  const pitches = new Set();
  return authored.map(note => {
    if (!Number.isInteger(note.pitch) || note.pitch < 45 || note.pitch > 67 ||
        !allowed.has(note.pitch % 12) || !Number.isInteger(note.velocity) ||
        note.velocity < 1 || note.velocity > 127 || pitches.has(note.pitch))
      throw new Error('AI returned an invalid or duplicate chord note');
    pitches.add(note.pitch);
    return { startBeat: 32, durationBeats: 4, pitch: note.pitch,
      velocity: note.velocity, channel: 0 };
  }).sort((a, b) => a.pitch - b.pitch);
}

export async function askModel(prompt, outputSchema = schema, outputName = 'pulso_chord_voicing') {
  const key = process.env.OPENAI_API_KEY;
  if (!key) throw new Error('OPENAI_API_KEY is unavailable');
  const headers = { Authorization: `Bearer ${key}`, 'Content-Type': 'application/json' };
  const body = { model: 'gpt-5.6-terra', background: true,
    reasoning: { effort: 'medium' }, max_output_tokens: 3000, input: prompt,
    text: { format: { type: 'json_schema', name: outputName, strict: true,
      schema: outputSchema } } };
  let reply = await fetch('https://api.openai.com/v1/responses', { method: 'POST',
    headers, body: JSON.stringify(body), signal: AbortSignal.timeout(30000) });
  let response = await reply.json();
  if (!reply.ok) throw new Error(`OpenAI request failed (${reply.status}): ${response.error?.code || ''}`);
  const deadline = Date.now() + 300000;
  while (['queued', 'in_progress'].includes(response.status) && Date.now() < deadline) {
    await new Promise(resolve => setTimeout(resolve, 4000));
    reply = await fetch(`https://api.openai.com/v1/responses/${encodeURIComponent(response.id)}`,
      { headers, signal: AbortSignal.timeout(30000) });
    response = await reply.json();
    if (!reply.ok) throw new Error(`OpenAI polling failed (${reply.status})`);
  }
  if (['queued', 'in_progress'].includes(response.status)) {
    await fetch(`https://api.openai.com/v1/responses/${encodeURIComponent(response.id)}/cancel`,
      { method: 'POST', headers, signal: AbortSignal.timeout(30000) }).catch(() => {});
    throw new Error('AI chord review timed out; original retained');
  }
  if (response.status !== 'completed')
    throw new Error(`AI chord review ${response.status}; original retained`);
  const content = response.output?.flatMap(item => item.content || [])
    .filter(item => item.type === 'output_text').map(item => item.text).join('');
  return { value: JSON.parse(content), responseId: response.id, usage: response.usage };
}

async function main() {
  for (const filename of ['full-song.mid', 'composition-plan.json', 'manifest.json', 'job.json'])
    if (!existsSync(path.join(sourceDir, filename))) throw new Error(`Missing ${filename}`);
  const originalBytes = readFileSync(path.join(sourceDir, 'full-song.mid'));
  const original = parseMidi(originalBytes);
  const plan = readJson('composition-plan.json');
  const manifest = readJson('manifest.json');
  const source = readJson('job.json');
  const owner = plan.cast?.find(part => part.id === 'cm9_foundation');
  const index = original.tracks.findIndex(track => track.name.includes(owner?.name || '\0'));
  const chord = plan.chord_palette?.find(item => item.id === 'i_Cm9');
  if (plan.title !== 'Sombra de Do' || !manifest.first_pass_audition || index < 1 ||
      !chord || original.beatsPerBar !== 4) throw new Error('Unexpected source score');
  const target = original.tracks[index].notes.filter(note => note.startBeat === 32);
  if (target.length !== 11 || target.some(note => note.durationBeats !== 4))
    throw new Error('The expected bar-9 chord is absent');
  const context = original.tracks.slice(1).map(track => ({ name: track.name,
    notes: ledger(track.notes.filter(n => n.startBeat >= 28 && n.startBeat < 40)) }));
  const prompt = [
    'You are the original composer revising one specific chord voicing of your finished original score.',
    'At bar 9 (absolute beat 32), the sustained primary Cm9 layer mistakenly stacks almost the entire scale: 11 simultaneous notes. A previous pitch-only edit sounded the same. Rewrite the WHOLE sustained chord, including deciding how many notes it needs, so it has a clear identity and voice-leading against the unchanged bass, pulse, atmosphere and lead. Retain a sustained four-beat chord. Do not merely transpose 11 pitches. Prefer the fewest meaningful chord tones; avoid muddy internal semitone clusters. The other tracks and every other bar will remain identical. Choose every pitch and velocity yourself. No procedural note generation will occur.',
    'Return notes only for the sustained chord at beat 32, duration 4. Pitch register MIDI 45–67. Its harmonic event is i_Cm9 with pitch classes C Eb G Bb D; the exact pitch-class set is 0, 3, 7, 10, 2. All returned pitches must be chord tones, not scale passing notes. The sub and pulse already supply C, Eb, G and Bb. Consider not duplicating every pulse tone. Explain your musical intention briefly.',
    `USER REQUEST ${JSON.stringify(source.prompt)}`,
    `CHORD ${JSON.stringify(chord)}`,
    `EXISTING MUSIC, notes [absolute beat, duration, pitch, velocity] ${JSON.stringify(context)}`,
  ].join('\n');
  const reply = await askModel(prompt);
  const authored = validateVoicing(reply.value.notes, new Set(chord.pitch_classes), target.length);
  const candidate = { ...original, tracks: original.tracks.map(track => ({ ...track,
    notes: [...track.notes] })) };
  candidate.tracks[index].notes = [...candidate.tracks[index].notes.filter(n => n.startBeat !== 32),
    ...authored].sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch);
  const bytes = replaceTracks(originalBytes, candidate, new Set([index]));
  const verified = parseMidi(bytes);
  if (verified.tracks.some((track, i) => track.notes.some(n => n.unterminated) ||
      (i !== index && JSON.stringify(ledger(track.notes)) !==
        JSON.stringify(ledger(original.tracks[i].notes)))))
    throw new Error('Edited MIDI altered an unrelated track or has hanging notes');
  if (JSON.stringify(ledger(verified.tracks[index].notes.filter(n => n.startBeat !== 32))) !==
      JSON.stringify(ledger(original.tracks[index].notes.filter(n => n.startBeat !== 32))))
    throw new Error('Edited MIDI altered notes outside bar 9');
  const id = randomUUID();
  const destination = path.join(jobsRoot, id);
  mkdirSync(destination, { recursive: false });
  const revisedManifest = structuredClone(manifest);
  revisedManifest.title = `${manifest.title} - A/B acorde compás 9`;
  revisedManifest.first_pass_audition = false;
  revisedManifest.review_audition = true;
  revisedManifest.comparisonFile = 'reference-standard.mid';
  revisedManifest.comparisonNotes = manifest.all_midi_notes;
  revisedManifest.all_midi_notes = verified.tracks.reduce((sum, t) => sum + t.notes.length, 0);
  revisedManifest.ai_authored_or_declared_transform_notes = revisedManifest.all_midi_notes;
  revisedManifest.tracks[index - 1].notes = verified.tracks[index].notes.length;
  revisedManifest.note_provenance = { direct_ai: authored.length,
    reused_ai: revisedManifest.all_midi_notes - authored.length,
    transformed_ai: 0, reused_transformed_ai: 0, other: 0 };
  revisedManifest.editorial = { technical_ready: true, musical_review_required: true,
    experimental_comparison_only: true };
  delete revisedManifest.musicalAuditFile;
  for (let i = 0; i < manifest.tracks.length; i++) {
    const filename = manifest.tracks[i].filename;
    const output = path.join(destination, filename);
    if (i + 1 === index) writeFileSync(output,
      singleTrackMidi(candidate, [candidate.tracks[index]]));
    else copyFileSync(path.join(sourceDir, filename), output);
  }
  for (const filename of ['composition-plan.json', 'phrase-architecture.json'])
    if (existsSync(path.join(sourceDir, filename)))
      copyFileSync(path.join(sourceDir, filename), path.join(destination, filename));
  writeFileSync(path.join(destination, 'full-song.mid'), bytes);
  writeFileSync(path.join(destination, 'reference-standard.mid'), originalBytes);
  writeFileSync(path.join(destination, 'manifest.json'), JSON.stringify(revisedManifest, null, 2));
  writeFileSync(path.join(destination, 'job.json'), JSON.stringify({ ...source,
    created_at: new Date().toISOString(), first_pass_audition: false,
    review_from_job: sourceId, experiment: 'ai-full-chord-voicing-ab' }, null, 2));
  writeFileSync(path.join(destination, 'local-origin.json'), JSON.stringify({ origin: 'auto' }));
  writeFileSync(path.join(destination, 'progress.json'), JSON.stringify({
    at: new Date().toISOString(), state: 'completed', stage: 'ready',
    completed: 1, total: 1, attempt: 0, detail: 'AI full chord-voicing A/B' }, null, 2));
  writeFileSync(path.join(destination, 'voicing-ab-report.json'), JSON.stringify({
    sourceJobId: sourceId, candidateJobId: id, responseId: reply.responseId,
    usage: reply.usage, intent: reply.value.musical_intent,
    before: ledger(target), after: ledger(authored), targetBar: 9 }, null, 2));
  process.stdout.write(JSON.stringify({ id, before: target.length, after: authored.length,
    pitches: authored.map(n => n.pitch), intent: reply.value.musical_intent }) + '\n');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
  main().catch(error => { process.stderr.write(`${error.message}\n`); process.exitCode = 1; });
