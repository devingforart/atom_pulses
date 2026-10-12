// Read-only A/B report. Never treats equal seed as proof of equal AI output.
import { existsSync, readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseMidi } from '../web/shared/midi.mjs';

export function summarizeComposition(directory) {
  const readJson = file => JSON.parse(readFileSync(path.join(directory, file), 'utf8'));
  const request = readJson('job.json');
  const manifest = readJson('manifest.json');
  const audit = readJson('musical-audit.json');
  const midi = parseMidi(readFileSync(path.join(directory, manifest.fullFile || 'full-song.mid')));
  const activeTracks = midi.tracks.filter(track => track.notes.length);
  const noteCounts = activeTracks.map(track => track.notes.length).sort((a, b) => a - b);
  const report = { title: manifest.title, jobId: path.basename(directory),
    prompt: request.prompt, seed: request.seed, requestedSeconds: request.duration_seconds,
    activeTracks: activeTracks.length, notes: noteCounts.reduce((sum, count) => sum + count, 0),
    fewestTrackNotes: noteCounts[0] || 0,
    medianTrackNotes: noteCounts.length ? noteCounts[Math.floor(noteCounts.length / 2)] : 0,
    technicalReady: audit.technical_ready, narrativeReady: audit.narrative_ready,
    soundscapeReady: audit.soundscape_ready,
    harmonicFloorCoverage: audit.harmonic_floor_coverage,
    harmonicConflicts: audit.harmonic_conflicts,
    lowRegisterConflicts: audit.low_register_conflicts,
    unsupportedChromatic: audit.unsupported_chromatic,
    invalidSustains: audit.invalid_sustains,
    resolutionScore: audit.resolution_score,
    causalNarrative: audit.causal_narrative,
    narrativeIssues: audit.narrative_issues,
    soundscapeIssues: audit.soundscape_issues };
  const apiTrace = path.join(directory, 'api-events.jsonl');
  if (existsSync(apiTrace)) {
    const calls = readFileSync(apiTrace, 'utf8').split(/\r?\n/).filter(Boolean)
      .map(line => JSON.parse(line)).filter(event => event.event === 'api_call');
    report.apiCalls = calls.length;
    report.apiMinutes = Number((calls.reduce((sum, call) => sum + (call.elapsed_ms || 0), 0) /
      60000).toFixed(1));
    report.writingAndRepairMinutes = Number((calls.filter(call =>
      !['pulso_song_macro', 'pulso_cast_manifest', 'pulso_cast_detail',
        'pulso_cast_supplement'].includes(call.phase))
      .reduce((sum, call) => sum + (call.elapsed_ms || 0), 0) / 60000).toFixed(1));
  }
  return report;
}

function main() {
  const [baseline, candidate] = process.argv.slice(2);
  if (!baseline || !candidate)
    throw new Error('Usage: node tools/compare-local-compositions.mjs <baseline-job-dir> <candidate-job-dir>');
  const before = summarizeComposition(baseline);
  const after = summarizeComposition(candidate);
  if (before.prompt !== after.prompt || before.seed !== after.seed ||
      before.requestedSeconds !== after.requestedSeconds)
    throw new Error('A/B requests differ in prompt, seed or duration');
  const objectiveChecks = {
    allRequestedTracks: after.activeTracks >= before.activeTracks,
    technicalIntegrity: after.technicalReady === true,
    narrativeGate: after.narrativeReady === true,
    harmonicFloor: after.harmonicFloorCoverage >= before.harmonicFloorCoverage - .02,
    harmonicConflicts: after.harmonicConflicts <= before.harmonicConflicts,
    lowRegisterConflicts: after.lowRegisterConflicts <= before.lowRegisterConflicts,
    chromaticIntegrity: after.unsupportedChromatic <= before.unsupportedChromatic,
    noteDurationIntegrity: after.invalidSustains <= before.invalidSustains,
  };
  process.stdout.write(`${JSON.stringify({ baseline: before, candidate: after,
    objectiveChecks, readyForHumanAudition: Object.values(objectiveChecks).every(Boolean) }, null, 2)}\n`);
}

if (process.argv[1] && path.resolve(process.argv[1]) === fileURLToPath(import.meta.url))
  main();
