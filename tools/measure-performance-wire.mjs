import { readdirSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { isDeepStrictEqual } from 'node:util';

const dir = process.argv[2] || 'build/local-studio-data/validated-response-cache';
const noteKeys = { beat: 'b', duration: 'd', pitch: 'p', velocity: 'v', voice: 'r', instrument_id: 'i', metric_intent: 'm' };
const controlKeys = { beat: 'b', controller: 'cc', value: 'v', voice: 'r', instrument_id: 'i' };
const remap = (event, keys) => Object.fromEntries(Object.entries(event).map(([key, value]) => [keys[key] || key, value]));
const restore = (event, keys) => remap(event, Object.fromEntries(Object.entries(keys).map(([longKey, shortKey]) => [shortKey, longKey])));
const totals = { cachedPerformanceRequests: 0, validResponses: 0, skippedCompactResponses: 0, notes: 0, controls: 0, originalBytes: 0, normalizedOriginalBytes: 0, compactBytes: 0, denseEventBytes: 0, denseSemanticBytes: 0, denseObjectBytes: 0, nonStrictNotes: 0, mismatchedVoiceNotes: 0, unknownOwnerNotes: 0 };

for (const file of readdirSync(dir)) {
  let envelope;
  try { envelope = JSON.parse(readFileSync(path.join(dir, file), 'utf8')); } catch { continue; }
  if (!envelope.request?.includes('pulso_performance_block')) continue;
  totals.cachedPerformanceRequests++;
  let response;
  try { response = JSON.parse(envelope.response); } catch { continue; }
  const texts = (response.output || []).flatMap(item => (item.content || [])
    .filter(content => typeof content.text === 'string').map(content => content.text));
  for (const text of texts) {
    let score;
    try { score = JSON.parse(text); } catch { continue; }
    if (!score.performance_score?.cells?.length) continue;
    if (score.performance_score.cells.some(cell => (cell.notes || []).some(note => Object.hasOwn(note, 'b')))) {
      totals.skippedCompactResponses++;
      continue;
    }
    totals.validResponses++;
    totals.originalBytes += Buffer.byteLength(text);
    totals.normalizedOriginalBytes += Buffer.byteLength(JSON.stringify(score));
    const exactDense = structuredClone(score);
    const semanticDense = structuredClone(score);
    const objectDense = structuredClone(score);
    const ownerIds = score.covered_instrument_ids || [];
    for (let index = 0; index < score.performance_score.cells.length; index++) {
      const cell = score.performance_score.cells[index];
      exactDense.performance_score.cells[index].notes = (cell.notes || []).map(note =>
        [note.beat, note.duration, note.pitch, note.velocity, note.voice, note.instrument_id, note.metric_intent].join('|'));
      exactDense.performance_score.cells[index].controls = (cell.controls || []).map(control =>
        [control.beat, control.controller, control.value, control.voice, control.instrument_id].join('|'));
      semanticDense.performance_score.cells[index].notes = (cell.notes || []).map(note => {
        if (note.metric_intent !== 'strict_grid') totals.nonStrictNotes++;
        if (!ownerIds.includes(note.instrument_id)) totals.unknownOwnerNotes++;
        return [note.beat, note.duration, note.pitch, note.velocity, ownerIds.indexOf(note.instrument_id)].join('|');
      });
      semanticDense.performance_score.cells[index].controls = (cell.controls || []).map(control =>
        [control.beat, control.controller, control.value, ownerIds.indexOf(control.instrument_id)].join('|'));
      objectDense.performance_score.cells[index].notes = (cell.notes || []).map(note =>
        ({ b: note.beat, d: note.duration, p: note.pitch, v: note.velocity, i: ownerIds.indexOf(note.instrument_id) }));
      objectDense.performance_score.cells[index].controls = (cell.controls || []).map(control =>
        ({ b: control.beat, cc: control.controller, v: control.value, i: ownerIds.indexOf(control.instrument_id) }));
    }
    totals.denseEventBytes += Buffer.byteLength(JSON.stringify(exactDense));
    totals.denseSemanticBytes += Buffer.byteLength(JSON.stringify(semanticDense));
    totals.denseObjectBytes += Buffer.byteLength(JSON.stringify(objectDense));
    for (const cell of score.performance_score.cells) {
      totals.notes += cell.notes?.length || 0;
      totals.controls += cell.controls?.length || 0;
      cell.notes = (cell.notes || []).map(note => {
        const compact = remap(note, noteKeys);
        if (!isDeepStrictEqual(restore(compact, noteKeys), note)) throw new Error('Note wire mapping lost information');
        return compact;
      });
      cell.controls = (cell.controls || []).map(control => {
        const compact = remap(control, controlKeys);
        if (!isDeepStrictEqual(restore(compact, controlKeys), control)) throw new Error('Control wire mapping lost information');
        return compact;
      });
    }
    totals.compactBytes += Buffer.byteLength(JSON.stringify(score));
  }
}
totals.byteReductionPercent = Number(((1 - totals.compactBytes / totals.originalBytes) * 100).toFixed(1));
totals.keyOnlyReductionPercent = Number(((1 - totals.compactBytes / totals.normalizedOriginalBytes) * 100).toFixed(1));
totals.denseExactReductionPercent = Number(((1 - totals.denseEventBytes / totals.normalizedOriginalBytes) * 100).toFixed(1));
totals.denseSemanticReductionPercent = Number(((1 - totals.denseSemanticBytes / totals.normalizedOriginalBytes) * 100).toFixed(1));
totals.denseObjectReductionPercent = Number(((1 - totals.denseObjectBytes / totals.normalizedOriginalBytes) * 100).toFixed(1));
console.log(JSON.stringify(totals, null, 2));
