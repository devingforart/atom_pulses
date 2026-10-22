import { readFileSync, writeFileSync, existsSync } from 'node:fs';
import { createHash } from 'node:crypto';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseMidi } from '../web/shared/midi.mjs';
import { auditClubScore } from './audit-club-score.mjs';
import { auditGrooveDirectory } from './audit-club-groove.mjs';

// Optional paid, read-only musical review. Does not generate or replace MIDI.
// Uses the same configured model as the composer and records its own usage.
const directory = path.resolve(process.argv[2] || '');
if (!process.argv[2]) throw new Error('Usage: node scripts/review-club-score.mjs <completed-job-directory>');
const read = name => JSON.parse(readFileSync(path.join(directory, name), 'utf8'));
const request = read('job.json'), manifest = read('manifest.json'), plan = read('composition-plan.json');
const production = existsSync(path.join(directory, 'club-production-plan.json')) ? read('club-production-plan.json') : null;
if (request.behavior !== 'club' || manifest.composition_behavior !== 'club') throw new Error('Not a completed Club score');
if (!process.env.OPENAI_API_KEY) throw new Error('OPENAI_API_KEY is not configured');
const config = readFileSync(fileURLToPath(new URL('../src/plugin/AiModelConfig.h', import.meta.url)), 'utf8');
const model = config.match(/model\s*=\s*"([^"]+)"/)?.[1];
if (!model) throw new Error('Composer model configuration missing');
const hash = () => createHash('sha256').update(readFileSync(path.join(directory, manifest.fullFile))).digest('hex');
const scoreHash = hash();
const tracks = manifest.tracks.map(track => {
  if (path.basename(track.filename) !== track.filename) throw new Error('Invalid MIDI path');
  const owner = plan.cast.find(part => part.name === track.name);
  return { id: owner?.id || track.filename, name: track.name,
    notes: parseMidi(readFileSync(path.join(directory, track.filename))).tracks.flatMap(item => item.notes)
      .map(note => [note.pitch, note.startBeat, note.durationBeats, note.velocity]) };
});
if (tracks.reduce((count, track) => count + track.notes.length, 0) > 60000)
  throw new Error('Score exceeds bounded full-note review capacity; no API request sent');
const schema = { type: 'object', additionalProperties: false,
  required: ['verdict', 'summary', 'strengths', 'issues', 'listening_cues'], properties: {
    verdict: { type: 'string', enum: ['ready_for_human_listening', 'needs_revision'] },
    summary: { type: 'string' }, strengths: { type: 'array', items: { type: 'string' } },
    listening_cues: { type: 'array', items: { type: 'string' } },
    issues: { type: 'array', items: { type: 'object', additionalProperties: false,
      required: ['severity', 'first_bar', 'last_bar', 'instrument_ids', 'evidence', 'proposed_ai_revision'],
      properties: { severity: { type: 'string', enum: ['minor', 'major'] },
        first_bar: { type: 'integer' }, last_bar: { type: 'integer' },
        instrument_ids: { type: 'array', items: { type: 'string' } },
        evidence: { type: 'string' }, proposed_ai_revision: { type: 'string' } } } } } };
const input = 'Act as an independent electronic club MIDI composition reviewer. Answer in Spanish. '
  + 'You have actual note data, not audio. Never claim to have listened. Evaluate whether the '
  + 'exported notes realize a coherent pitched club arrangement: stable recognizable bass/arp/stab '
  + 'cells, complementary sustained voiced chords and texture, a memorable restrained hook, '
  + 'prepared tension/withdrawal/return, and an intelligible ending. Literal repetitions and long '
  + 'plateaus are intentional strengths when coherent; do not impose chamber-music novelty, '
  + 'conjunct-motion quotas or compulsory 64-bar changes. Drums/percussion are intentionally absent. '
  + 'Diatonic notes are not automatically consonant: inspect simultaneous sustained voicings, '
  + 'bass/arp compatibility with changing chords, excessive same-pitch retriggers and unsupported '
  + 'minor-second clusters. Distinguish purposeful extensions/passing tones from actual unresolved '
  + 'clashes. Do not invent defects from a note-count metric or praise blueprint prose not enacted '
  + 'in the notes. Cite exact instrument IDs, actual MIDI pitches, absolute beats and ONE-BASED '
  + 'bar ranges for each concrete issue. A major issue requires needs_revision; no major issues '
  + 'permits ready_for_human_listening, not a guarantee of professional audio quality. Propose only '
  + 'Do not repeat the earlier false-positive review: a diatonic ensemble with some literal '
  + 'recurrence and sections named tension/return can still be musically muddled and unusable. '
  + 'Inspect the joint attack patterns: is a consistent metrical backbone actually carried by '
  + 'bass/stabs/arp, or are several unrelated offbeat motifs merely superimposed? Identify which '
  + 'part carries the subdivision and how the other parts reinforce it or leave intelligible '
  + 'gaps. Examine the hook as a recognizable phrase with space, rather than disconnected cues. '
  + 'For a short four-part nucleus, sustained pads/textures and a full long-song narrative are '
  + 'deliberately absent: evaluate whether this core can sustain a club track, including the '
  + 'audible withdrawal/return, not whether it is already a finished five-minute production. '
  + 'Use attack evidence rather than declaring that long pads, zero semitone collisions or '
  + 'high note counts prove a groove. State uncertainty when symbolic evidence cannot settle it. '
  + 'The listener rejected the previous technically stable version as skeletal and incomplete: '
  + 'the foreground was a tiny cue repeated with little continuation and the sequence was '
  + 'punctuation rather than an enveloping developed figure. Evaluate actual musical substance '
  + 'and phrase consequence, not only tonal safety and stable cycles. More notes alone are '
  + 'not improvement. When musical_arcs exist, contrast their intended subject/continuation/'
  + 'arrival with the actual notes; never praise the declared story as though it were realized. '
  + 'Verify interval arithmetic: absolute MIDI difference 22 is a compound minor seventh, '
  + 'not a minor ninth; differences 13 and 25 are compound minor seconds. Do not reject '
  + 'ordinary chord extensions merely because the chord is not a triad. When groove_core_ids '
  + 'is present, inspect its actual phase, repetition, articulation and mutual rhythmic space '
  + 'separately from the supporting pads/hooks. A phase match is not a musical endorsement. '
  + 'When production.format=continuous_section, evaluate a FULL ELECTRONIC PRODUCTION SECTION: '
  + 'actual voiced sustained harmonic bodies/pads, independent recurring sequences, rhythmic '
  + 'chords, propulsive bass, useful pitched atmosphere, coherent simultaneous registers and '
  + 'musical variation within a sustainable state. Do not require a miniature intro/breakdown/'
  + 'drop/outro, a vocal protagonist, a withdrawal/return or a final classical cadence. The end '
  + 'must support a musical wrap or continuation. Layer plans are promises to VERIFY against '
  + 'notes, not evidence of success. A sparse four-part chamber dialogue is not adequate for '
  + 'a requested rich twelve-layer electronic body. Do not equate aligned onsets with danceability. '
  + 'Only propose focused AI-authored revisions retaining good material. Do not output rewritten notes. '
  + 'Tuple format [MIDI pitch, ABSOLUTE start beat, sounding duration in beats, velocity].\n'
  + JSON.stringify({ request: { prompt: request.prompt, behavior: request.behavior,
    allow_chromatic: request.allow_chromatic, duration_seconds: request.duration_seconds,
    bpm: request.bpm }, harmonicTimeline: {
      key: plan.key, beatsPerBar: plan.beats_per_bar, bars: plan.total_bars,
      chords: plan.chord_palette,
      sections: plan.sections.map(section => ({ start_bar: section.start_bar,
        bars: section.bars, harmonic_events: section.harmonic_events })) },
    // Deliberately omit narrative prose, declared energy and named dramatic acts:
    // they primed earlier critics to praise intentions not realized in the MIDI.
    production, measurements: auditClubScore(directory),
    groove: auditGrooveDirectory(directory), tracks });
const headers = { Authorization: `Bearer ${process.env.OPENAI_API_KEY}`, 'Content-Type': 'application/json' };
const started = Date.now(), deadline = started + 240000;
async function api(url, options = {}) {
  const result = await fetch(`https://api.openai.com/v1/responses${url}`, {
    ...options, headers, signal: AbortSignal.timeout(Math.max(1, Math.min(60000, deadline - Date.now()))) });
  if (!result.ok) throw new Error(`Review API returned HTTP ${result.status}`);
  return result.json();
}
let response = await api('', { method: 'POST', body: JSON.stringify({ model, background: true,
  reasoning: { effort: 'medium' }, max_output_tokens: 6000, input,
  text: { format: { type: 'json_schema', name: 'club_midi_review', strict: true, schema } } }) });
if (typeof response.id !== 'string' || !/^resp_[a-zA-Z0-9_-]+$/.test(response.id)) throw new Error('Invalid response ID');
while (['queued', 'in_progress'].includes(response.status) && Date.now() < deadline) {
  await new Promise(resolve => setTimeout(resolve, 1500));
  response = await api(`/${response.id}`);
}
if (response.status !== 'completed') {
  if (['queued', 'in_progress'].includes(response.status)) {
    // Cancel this same response, never silently launch a second paid attempt.
    await fetch(`https://api.openai.com/v1/responses/${response.id}/cancel`, {
      method: 'POST', headers, body: '{}', signal: AbortSignal.timeout(10000) }).catch(() => {});
  }
  throw new Error(`Review did not complete: ${response.status}`);
}
const text = (response.output || []).flatMap(item => item.content || [])
  .filter(item => item.type === 'output_text').map(item => item.text).join('');
const review = JSON.parse(text);
if (hash() !== scoreHash) throw new Error('MIDI changed during review');
const report = { ...review, score_sha256: scoreHash, model, elapsed_ms: Date.now() - started,
  usage: response.usage, source: 'read-only AI review of actual exported notes; human listening still required' };
writeFileSync(path.join(directory, 'club-ai-review.json'), JSON.stringify(report, null, 2));
console.log(JSON.stringify(report, null, 2));
