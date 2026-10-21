// Read-only, advisory paired MIDI critique. It never edits or rejects a score.
// A model can examine symbolic evidence; it does not hear the rendered audio.
import fs from 'node:fs';
import path from 'node:path';
import { randomInt } from 'node:crypto';
import { setTimeout as delay } from 'node:timers/promises';
import { parseMidi } from '../web/shared/midi.mjs';

const [left, right, destination] = process.argv.slice(2);
if (!left || !right || !destination)
  throw new Error('Usage: node tools/compare-musical-discourse.mjs <directory-A> <directory-B> <output.json>');
if (!process.env.OPENAI_API_KEY) throw new Error('OPENAI_API_KEY is not configured');
const sources = randomInt(2) ? [left, right] : [right, left];
const labels = ['X', 'Y'];
const round = number => Math.round(number * 10000) / 10000;
const pitchNames = ['C', 'C#', 'D', 'Eb', 'E', 'F', 'F#', 'G', 'Ab', 'A', 'Bb', 'B'];
const noteName = pitch => pitchNames[pitch % 12] + (Math.floor(pitch / 12) - 1);
const scores = sources.map((directory, index) => {
  const read = file => JSON.parse(fs.readFileSync(path.join(directory, file), 'utf8'));
  const manifest = read('manifest.json');
  const plan = read(manifest.planFile || 'composition-plan.json');
  const midi = parseMidi(fs.readFileSync(path.join(directory, manifest.fullFile || 'full-song.mid')));
  return { label: labels[index], direction: read('job.json').prompt,
    bpm: plan.bpm || manifest.bpm, beatsPerBar: plan.beats_per_bar,
    key: plan.key, protagonist: plan.protagonist_id,
    sections: plan.sections.map(section => ({ name: section.name, start: section.start_bar,
      bars: section.bars, harmony: section.harmonic_events })),
    parts: midi.tracks.filter(track => track.notes.length).map(track => ({
      name: track.name,
      events: track.notes.map(note => [round(note.startBeat), round(note.durationBeats),
        note.pitch, note.velocity, noteName(note.pitch)]),
    })) };
});
const citedEvidenceVariants = scores.flatMap(score => score.parts.map(part => ({
  type: 'object', additionalProperties: false,
  required: ['score', 'part', 'absolute_beats', 'observation'],
  properties: {
    score: { type: 'string', enum: [score.label] },
    part: { type: 'string', enum: [part.name] },
    absolute_beats: { type: 'array', minItems: 1, maxItems: 8,
      items: { type: 'number', enum: [...new Set(part.events.map(event => event[0]))] } },
    observation: { type: 'string' },
  },
})));
const schema = { type: 'object', additionalProperties: false,
  required: ['preferred', 'reasoning', 'evidence', 'remaining_risks', 'confidence'],
  properties: {
    preferred: { type: 'string', enum: ['X', 'Y', 'tie', 'neither'] },
    reasoning: { type: 'string' },
    evidence: { type: 'array', minItems: 2, maxItems: 10, items: { anyOf: citedEvidenceVariants } },
    remaining_risks: { type: 'array', maxItems: 6, items: { type: 'string' } },
    confidence: { type: 'string', enum: ['low', 'medium', 'high'] },
  } };
const instruction = `Compare two anonymous complete MIDI scores as a chamber-music editor.
You have SYMBOLIC NOTE EVIDENCE, not audio. Never claim to have listened.
Notes are [absolute zero-based beat, sounding duration in beats, MIDI pitch, velocity,
verified scientific pitch name with C4=60]. Use these verified names; do not invent
different pitch names from memory or confuse one part's notes with another part.
Assess an audible musical argument: a recognizable proposition, continuation beyond
its clue, a related answer, preparation and consequence of intensification, and earned
melodic as well as harmonic closure. Inspect the ACTUAL notes rather than trusting
section titles. Each absolute_beats citation MUST be an actual note-on time of
the named part. Consider shared pulse, meaningful accent hierarchy, moving-vs-held
relationships and whether interweaving voices remain comprehensible as an ensemble.
The score may be auditioned through one piano. Distinguish intentional echoes or
unisons from accidental near reattacks obscuring a held thought. Counterpoint, Bartokian
angularity, fast playing, chromatic tension and overlapping lines are NOT inherently bad.
Do not reward fewer notes, uniformly slower writing, thin accompaniment, homogeneous
rhythm or mere consonance. A slower but less developed melody may be inferior.
Choose tie or neither if appropriate. Cite specific true pitches/times/continuations
in each score, including its ending, rather than generic praise. Evidence must refer
to exact part names and absolute beats present in the supplied scores. State remaining
musical weaknesses even for the preferred candidate. This critique is advisory and
cannot establish human listening preference. Return concise JSON in Spanish.`;
const headers = { Authorization: `Bearer ${process.env.OPENAI_API_KEY}`,
  'Content-Type': 'application/json' };
const started = Date.now();
async function request(url, options = {}) {
  const response = await fetch(url, { ...options, headers, signal: AbortSignal.timeout(60000) });
  if (!response.ok) throw new Error(`OpenAI critique HTTP ${response.status}; no automatic retry`);
  return response.json();
}
let response = await request('https://api.openai.com/v1/responses', { method: 'POST',
  body: JSON.stringify({ model: 'gpt-5.6-terra', background: true,
    reasoning: { effort: 'medium' }, max_output_tokens: 4000,
    instructions: instruction, input: JSON.stringify(scores),
    text: { format: { type: 'json_schema', name: 'musical_discourse_comparison', strict: true, schema } } }) });
const id = response.id;
if (!/^resp_[A-Za-z0-9]+$/.test(id)) throw new Error('Missing critique response ID');
while (['queued', 'in_progress'].includes(response.status)) {
  if (Date.now() - started > 600000)
    throw new Error(`Critique still pending; retrieve the same response ${id}, do not resubmit`);
  await delay(2500);
  response = await request(`https://api.openai.com/v1/responses/${id}`);
}
if (response.status !== 'completed') throw new Error(`Critique ended ${response.status}; no automatic retry`);
const text = (response.output || []).flatMap(item => item.content || [])
  .filter(item => item.type === 'output_text').map(item => item.text).join('');
const critique = JSON.parse(text);
const eventSets = scores.map(score => new Map(score.parts.map(part =>
  [part.name, new Set(part.events.map(event => event[0]))])));
const evidenceVerification = critique.evidence.map(item => {
  const onsets = eventSets[labels.indexOf(item.score)]?.get(item.part);
  return { ...item, exactPartExists: Boolean(onsets),
    citedAttacksExist: Boolean(onsets) && item.absolute_beats.every(beat => onsets.has(beat)) };
});
const report = { advisoryOnly: true, method: 'Anonymous paired symbolic-MIDI critique; not audio listening',
  requiresHumanEvidenceReview: true, musicalQualityEstablished: false,
  citationCheckScope: 'Exact part identity and note-on times only; not natural-language pitch assertions or artistic judgment',
  labels: Object.fromEntries(labels.map((label, index) => [label, path.resolve(sources[index])])),
  elapsedSeconds: round((Date.now() - started) / 1000), responseId: id,
  model: response.model, usage: response.usage, critique, evidenceVerification };
fs.writeFileSync(path.resolve(destination), JSON.stringify(report, null, 2) + '\n');
const verified = evidenceVerification.length >= 2 &&
  labels.every(label => evidenceVerification.some(item => item.score === label)) &&
  evidenceVerification.every(item => item.citedAttacksExist);
console.log(JSON.stringify({ preferred: verified ? critique.preferred : 'unverified',
  source: verified ? report.labels[critique.preferred] || null : null, confidence: critique.confidence,
  citationCoordinatesVerified: evidenceVerification.every(item => item.citedAttacksExist),
  semanticClaimsVerified: false,
  output: path.resolve(destination) }));
