import test from 'node:test';
import assert from 'node:assert/strict';
import { summarizeLatency } from '../tools/analyze-local-latency.mjs';

test('latency report separates writing, repairs, and split transactions', () => {
  const calls = [
    { event: 'api_call', at: '2026-10-11T00:00:50Z', phase: 'pulso_performance_block', elapsed_ms: 50000,
      input_tokens: 9000, output_tokens: 4000, connected: true, http_status: 200 },
    { event: 'api_call', at: '2026-10-11T00:01:02Z', phase: 'pulso_compact_voicing_candidates', elapsed_ms: 12000,
      input_tokens: 2000, output_tokens: 600, connected: true, http_status: 200 },
  ];
  const progress = [
    { at: '2026-10-11T00:00:00Z', stage: 'writing', detail: 'instrument block 1/2 - 2 parts' },
    { at: '2026-10-11T00:00:50Z', stage: 'recovery', detail: 'AI separating discordant voices for independent harmonic review' },
    { at: '2026-10-11T00:01:00Z', stage: 'recovery', detail: 'AI refining 3 measured chord attacks; accepted MIDI preserved' },
    { at: '2026-10-11T00:02:00Z', stage: 'writing', detail: 'instrument block 2/2 - 1 parts' },
  ];
  const report = summarizeLatency(calls, progress, { id: 'example', state: 'completed',
    request: { duration_seconds: 600 }, telemetry: { wallMs: 120000 } });
  assert.equal(report.apiMs, 62000);
  assert.equal(report.plannedBlocks, 2);
  assert.equal(report.groupedSplits, 1);
  assert.equal(report.compactRepairs, 1);
  assert.equal(report.blocks[0].apiCalls, 2);
  assert.equal(report.blocks[0].apiMs, 62000);
  assert.equal(report.phases[0].phase, 'pulso_performance_block');
});
