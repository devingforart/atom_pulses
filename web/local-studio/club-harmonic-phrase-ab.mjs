// Controlled, AI-authored rewrite of an underwritten harmonic phrase.
// All other MIDI tracks and all other time windows are preserved exactly.
import { copyFileSync, existsSync, mkdirSync, readFileSync, writeFileSync } from 'node:fs';
import { randomUUID } from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseMidi, singleTrackMidi } from '../shared/midi.mjs';
import { replaceTracks } from './phrase-dialogue-experiment.mjs';
import { askModel } from './club-voicing-ab.mjs';
import { jobDirectory, jobsRoot } from './storage.mjs';

const sourceId = process.argv.find(arg => arg.startsWith('--source='))?.slice(9) ||
  '1e8675dd-3ed5-49ef-b125-3b3b02277fd0';
const sourceDir = jobDirectory(sourceId);
const window = [32, 48];
const readJson = name => JSON.parse(readFileSync(path.join(sourceDir, name), 'utf8'));
const ledger = notes => notes.map(n => [n.startBeat, n.durationBeats, n.pitch, n.velocity]);
const inWindow = note => note.startBeat >= window[0] && note.startBeat < window[1];
const noteSchema = { type: 'object', additionalProperties: false, properties: {
  beat: { type: 'number' }, duration: { type: 'number' },
  pitch: { type: 'integer' }, velocity: { type: 'integer' },
}, required: ['beat', 'duration', 'pitch', 'velocity'] };
const schema = { type: 'object', additionalProperties: false, properties: {
  diagnosis: { type: 'string' }, musical_intent: { type: 'string' },
  notes: { type: 'array', items: noteSchema },
}, required: ['diagnosis', 'musical_intent', 'notes'] };

export function validatePhrase(authored, chords, register, range = window) {
  if (!Array.isArray(authored) || !authored.length || authored.length > 80)
    throw new Error('AI phrase is empty or unreasonably large');
  const notes = authored.map(({ beat, duration, pitch, velocity }) => {
    if (![beat, duration, pitch, velocity].every(Number.isFinite) ||
        !Number.isInteger(pitch) || !Number.isInteger(velocity) ||
        pitch < register[0] || pitch > register[1] || velocity < 1 || velocity > 127 ||
        beat < range[0] || beat >= range[1] || duration < 0.25 ||
        beat + duration > range[1] + 0.0001 ||
        Math.abs(beat * 4 - Math.round(beat * 4)) > 0.0001 ||
        Math.abs(duration * 4 - Math.round(duration * 4)) > 0.0001)
      throw new Error(`AI authored an out-of-bounds note ${JSON.stringify({ beat,
        duration, pitch, velocity })}`);
    const firstBar = Math.floor((beat - range[0]) / 4);
    const lastBar = Math.floor((beat + duration - range[0] - 0.0001) / 4);
    for (let bar = firstBar; bar <= lastBar; bar++)
      if (!chords[bar]?.has(pitch % 12))
        throw new Error(`AI authored a non-chord tone over bar ${bar + 9}`);
    return { startBeat: beat, durationBeats: duration, pitch, velocity, channel: 0 };
  }).sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch);
  for (let i = 0; i < notes.length; i++)
    for (let j = i + 1; j < notes.length &&
        notes[j].startBeat < notes[i].startBeat + notes[i].durationBeats - 0.0001; j++)
      if (notes[i].pitch === notes[j].pitch)
        throw new Error('AI authored overlapping same-pitch chord notes');
  for (let beat = range[0]; beat < range[1]; beat += 0.25)
    if (!notes.some(note => note.startBeat <= beat + 0.0001 &&
        note.startBeat + note.durationBeats > beat + 0.0001))
      throw new Error(`AI left the sustained harmonic body silent at beat ${beat}`);
  return notes;
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
  if (plan.title !== 'Sombra de Do' || !manifest.first_pass_audition || index < 1 ||
      original.beatsPerBar !== 4) throw new Error('Unexpected source score');
  const chordIds = ['i_Cm9', 'iv_Fm7', 'VI_Abmaj7', 'v_Gm7'];
  const palette = chordIds.map(id => plan.chord_palette?.find(chord => chord.id === id));
  if (palette.some(chord => !chord)) throw new Error('Harmonic palette is incomplete');
  const before = original.tracks[index].notes.filter(inWindow);
  if (before.length !== 11 || before.some(n => n.startBeat !== 32))
    throw new Error('Expected underwritten bar-9-to-12 phrase is absent');
  if (original.tracks[index].notes.some(n => n.startBeat < window[0] &&
      n.startBeat + n.durationBeats > window[0]))
    throw new Error('A note from before the rewrite overlaps the phrase');
  const context = original.tracks.slice(1).map(track => ({ name: track.name,
    notes: ledger(track.notes.filter(n => n.startBeat >= 28 && n.startBeat < 52)) }));
  const prompt = [
    'You are the original composer/editor of a completed club piece. Rewrite the sustained PRIMARY HARMONIC BODY across bars 9–12, absolute beats 32–48 (4/4), as one coherent phrase. The current track has an 11-note C-minor scale cluster at beat 32 that lasts one bar, then complete silence in bars 10–12. This contradicts your plan: a sustained bed with voice-leading through i–iv–VI–v. A previous one-chord repair preserved the silence and failed audibly. Revise the whole 4-bar phrase, not just bar 9.',
    'All bass, chord pulse, atmosphere, melody, other tracks, and all outside notes are immutable. Hear their current rhythms and pitches in the score below. Write only the primary sustained body, independently composed with economical voicings, shared tones and meaningful inner motion. Avoid the previous 11-note stack and avoid simply holding one chord over all four changes. You may vary voicing density and change onset/length as musically useful, but the sustained body should remain audible throughout this four-bar phrase, with no inexplicable empty bar. The target is club coherence, not chamber-style busy counterpoint. The low C and moving bass already supply root movement; the pulse provides repeated chord attacks.',
    'Choose every pitch, onset, duration and velocity yourself. This tool only validates and inserts your authored MIDI; it does not generate notes. Notes must be on a 0.25-beat grid, duration >=0.25, within beats 32–48 and MIDI pitch register 45–67. At bar 9 beats 32–36 use Cm9 tones C Eb G Bb D; bar 10 beats 36–40 Fm7 tones F Ab C Eb; bar 11 beats 40–44 Abmaj7 tones Ab C Eb G; bar 12 beats 44–48 Gm7 tones G Bb D F. A held note across a bar line must belong to both harmonies. Explain briefly how your phrase moves and answers the fixed accompaniment.',
    `USER REQUEST ${JSON.stringify(source.prompt)}`,
    `CHORD PALETTE ${JSON.stringify(palette)}`,
    `NEARBY MIDI [absolute beat,duration,pitch,velocity] ${JSON.stringify(context)}`,
  ].join('\n');
  const reply = await askModel(prompt, schema, 'pulso_harmonic_phrase');
  const authored = validatePhrase(reply.value.notes,
    palette.map(chord => new Set(chord.pitch_classes)),
    [owner.minimum_pitch, owner.maximum_pitch]);
  const candidate = { ...original, tracks: original.tracks.map(track => ({ ...track,
    notes: [...track.notes] })) };
  candidate.tracks[index].notes = [...candidate.tracks[index].notes.filter(n => !inWindow(n)),
    ...authored].sort((a, b) => a.startBeat - b.startBeat || a.pitch - b.pitch);
  const bytes = replaceTracks(originalBytes, candidate, new Set([index]));
  const verified = parseMidi(bytes);
  if (verified.tracks.some((track, i) => track.notes.some(n => n.unterminated) ||
      (i !== index && JSON.stringify(ledger(track.notes)) !==
        JSON.stringify(ledger(original.tracks[i].notes)))))
    throw new Error('Edited MIDI altered an unrelated track or has hanging notes');
  if (JSON.stringify(ledger(verified.tracks[index].notes.filter(n => !inWindow(n)))) !==
      JSON.stringify(ledger(original.tracks[index].notes.filter(n => !inWindow(n)))))
    throw new Error('Edited MIDI altered notes outside the phrase');
  validatePhrase(verified.tracks[index].notes.filter(inWindow).map(note => ({
    beat: note.startBeat, duration: note.durationBeats,
    pitch: note.pitch, velocity: note.velocity })),
    palette.map(chord => new Set(chord.pitch_classes)),
    [owner.minimum_pitch, owner.maximum_pitch]);
  const id = randomUUID();
  const destination = path.join(jobsRoot, id);
  mkdirSync(destination, { recursive: false });
  const revisedManifest = structuredClone(manifest);
  revisedManifest.title = `${manifest.title} - A/B frase armónica 9–12`;
  revisedManifest.first_pass_audition = false;
  revisedManifest.review_audition = true;
  revisedManifest.comparisonFile = 'reference-standard.mid';
  revisedManifest.comparisonNotes = manifest.all_midi_notes;
  revisedManifest.all_midi_notes = verified.tracks.reduce((sum, track) => sum + track.notes.length, 0);
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
    review_from_job: sourceId, experiment: 'ai-harmonic-phrase-ab' }, null, 2));
  writeFileSync(path.join(destination, 'local-origin.json'), JSON.stringify({ origin: 'auto' }));
  writeFileSync(path.join(destination, 'progress.json'), JSON.stringify({
    at: new Date().toISOString(), state: 'completed', stage: 'ready',
    completed: 1, total: 1, attempt: 0, detail: 'AI harmonic phrase review A/B' }, null, 2));
  writeFileSync(path.join(destination, 'harmonic-phrase-report.json'), JSON.stringify({
    sourceJobId: sourceId, candidateJobId: id, responseId: reply.responseId,
    usage: reply.usage, diagnosis: reply.value.diagnosis,
    intent: reply.value.musical_intent,
    before: ledger(before), after: ledger(authored), targetBars: [9, 12] }, null, 2));
  process.stdout.write(JSON.stringify({ id, before: before.length, after: authored.length,
    diagnosis: reply.value.diagnosis, intent: reply.value.musical_intent }) + '\n');
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
  main().catch(error => { process.stderr.write(`${error.message}\n`); process.exitCode = 1; });
