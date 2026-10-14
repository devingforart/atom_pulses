#!/usr/bin/env node
// Read-only, experimental score review. It does not approve or alter MIDI.
import { readFile } from 'node:fs/promises';
import path from 'node:path';

const [baselineDir, candidateDir] = process.argv.slice(2);
if (!baselineDir || !candidateDir) {
  console.error('Usage: node tools/evaluate-narrative-ab.mjs BASELINE_JOB_DIR CANDIDATE_JOB_DIR');
  process.exit(2);
}
const apiKey = process.env.OPENAI_API_KEY;
if (!apiKey) {
  console.error('OPENAI_API_KEY is required in the environment.');
  process.exit(2);
}

async function readScore(directory) {
  const plan = JSON.parse(await readFile(path.join(directory, 'composition-plan.json'), 'utf8'));
  const checkpoint = JSON.parse(await readFile(path.join(directory, 'resume-score.json'), 'utf8'));
  const protagonist = plan.protagonist_id;
  const answerer = plan.cast.find((part) => part.line_relationship === 'call_response')?.id;
  if (!protagonist || !answerer || !Array.isArray(checkpoint.notes))
    throw new Error(`Incomplete narrative MIDI in ${directory}`);
  const sections = plan.sections.map((scene, index) => {
    const startBeat = scene.start_bar * plan.beats_per_bar;
    const endBeat = (scene.start_bar + scene.bars) * plan.beats_per_bar;
    const lines = [protagonist, answerer].map((instrumentId, role) => {
      const notes = checkpoint.notes
        .filter((note) => note.instrument_id === instrumentId &&
          note.start_beat >= startBeat && note.start_beat < endBeat)
        .sort((a, b) => a.start_beat - b.start_beat || a.pitch - b.pitch);
      return `${role === 0 ? 'lead' : 'answer'}: ${notes.map((note) =>
        `${Number((note.start_beat - startBeat).toFixed(2))}:${note.pitch}/${Number(note.duration_beats.toFixed(2))}`
      ).join(' ') || 'rest'}`;
    });
    const harmony = scene.harmonic_events.map((event) =>
      `${event.bar_offset}:${event.chord_id}`).join(' ');
    return `scene ${index + 1}, bars ${scene.bars}, harmony ${harmony}\n${lines.join('\n')}`;
  });
  return `tempo ${plan.bpm ?? ''}, key ${plan.key}, sections ${plan.sections.length}\n${sections.join('\n')}`;
}

const [a, b] = await Promise.all([readScore(baselineDir), readScore(candidateDir)]);
const prompt = `Independent blind comparison of TWO symbolic MIDI scores. A and B share the same harmony, cast and structure; only their lead and answering voices are shown. Judge the likely AUDIBLE long-form narrative, not spelling, verbosity or note quantity. Identify a distinctive opening pitch-and-onset gesture from the actual MIDI; test whether later scenes recall and transform it in an intelligible causal sequence and whether the final scene answers it. An unchanged repeated lick is not development; unrelated virtuosic phrases are not a story. Don't infer anything about timbre or mix from this symbolic score. Ground each claim in scene numbers and specific pitches/rhythms. If neither is convincingly narrative, say tie and explain the remaining defect.\n\nSCORE A\n${a}\n\nSCORE B\n${b}`;
const schema = {
  type: 'object', additionalProperties: false,
  properties: {
    winner: { type: 'string', enum: ['A', 'B', 'tie'] },
    confidence: { type: 'number' },
    opening_gesture: { type: 'string' },
    causal_evidence: { type: 'array', items: { type: 'string' } },
    weak_scenes: { type: 'array', items: { type: 'string' } },
    limitation: { type: 'string' },
  },
  required: ['winner', 'confidence', 'opening_gesture', 'causal_evidence', 'weak_scenes', 'limitation'],
};
const response = await fetch('https://api.openai.com/v1/responses', {
  method: 'POST',
  headers: { Authorization: `Bearer ${apiKey}`, 'Content-Type': 'application/json' },
  body: JSON.stringify({
    model: process.env.PULSO_NARRATIVE_REVIEW_MODEL || 'gpt-5.6-terra',
    reasoning: { effort: 'medium' },
    max_output_tokens: 3500,
    input: prompt,
    text: { format: { type: 'json_schema', name: 'pulso_narrative_ab_review', strict: true, schema } },
  }),
  signal: AbortSignal.timeout(180000),
});
const payload = await response.json();
if (!response.ok || payload.status !== 'completed')
  throw new Error(`Narrative A/B response unavailable: HTTP ${response.status}, state ${payload.status || 'unknown'}`);
const output = payload.output?.flatMap((item) => item.content || [])
  .find((item) => item.type === 'output_text')?.text;
if (!output) throw new Error('Narrative A/B response contained no structured text.');
console.log(JSON.stringify({ review: JSON.parse(output), usage: payload.usage }, null, 2));
