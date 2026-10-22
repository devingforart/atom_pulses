import test from 'node:test';
import assert from 'node:assert/strict';
import { mkdtempSync, writeFileSync, readFileSync, rmSync } from 'node:fs';
import { tmpdir } from 'node:os';
import path from 'node:path';
import { singleTrackMidi } from '../web/shared/midi.mjs';
import { auditClubScore } from '../scripts/audit-club-score.mjs';

test('Club audit measures real recurrence and never rewrites the MIDI', () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'pulso-club-audit-'));
  try {
    const notes = Array.from({ length: 32 }, (_, i) => ({ startBeat: i,
      durationBeats: .5, pitch: [48, 55, 60, 55][i % 4], velocity: 80, channel: 1 }));
    const bytes = singleTrackMidi({ division: 480, beatsPerBar: 4 }, [{ notes }]);
    writeFileSync(path.join(dir, 'track.mid'), bytes);
    writeFileSync(path.join(dir, 'manifest.json'), JSON.stringify({ title: 'Retained cell',
      composition_behavior: 'club', tracks: [{ name: 'Arpeggio', filename: 'track.mid' }] }));
    writeFileSync(path.join(dir, 'composition-plan.json'), JSON.stringify({
      beats_per_bar: 4, total_bars: 8, sections: [{ name: 'State', start_bar: 0, bars: 8 }] }));
    const before = readFileSync(path.join(dir, 'track.mid'));
    const report = auditClubScore(dir);
    assert.equal(report.notes, 32);
    assert.equal(report.tracks[0].literalRecurrence, 1);
    assert.equal(report.tracks[0].longestLiteralPatternReturns, 2);
    assert.equal(report.tracks[0].samePitchOverlaps, 0);
    assert.equal(report.silentBars, 0);
    assert.equal(report.tracks[0].offGrid, 0);
    assert.deepEqual(readFileSync(path.join(dir, 'track.mid')), before);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});

test('held compound minor ninth evidence uses actual pitch arithmetic, not critic labels', () => {
  const dir = mkdtempSync(path.join(tmpdir(), 'pulso-club-intervals-'));
  try {
    const tracks = [
      [{ startBeat: 0, durationBeats: 8, pitch: 52, velocity: 70, channel: 1 },
        { startBeat: 16, durationBeats: 8, pitch: 62, velocity: 70, channel: 1 }],
      [{ startBeat: 0, durationBeats: 4, pitch: 77, velocity: 70, channel: 1 },
        { startBeat: 16, durationBeats: 4, pitch: 84, velocity: 70, channel: 1 }]
    ];
    tracks.forEach((notes, index) => writeFileSync(path.join(dir, `${index}.mid`),
      singleTrackMidi({ division: 480, beatsPerBar: 4 }, [{ notes }])));
    writeFileSync(path.join(dir, 'manifest.json'), JSON.stringify({ tracks:
      tracks.map((_, index) => ({ name: `Layer ${index}`, filename: `${index}.mid` })) }));
    writeFileSync(path.join(dir, 'composition-plan.json'), JSON.stringify({ beats_per_bar: 4,
      total_bars: 8, sections: [{ name: 'Section', start_bar: 0, bars: 8 }] }));
    const evidence = auditClubScore(dir);
    assert.equal(evidence.sustainedAdjacentPairs, 0);
    assert.equal(evidence.sustainedCompoundMinorNinthPairs, 1);
    assert.deepEqual(evidence.sustainedCompoundMinorNinthExamples[0].pitches, [52, 77]);
    assert.equal(evidence.notes, 4);
  } finally { rmSync(dir, { recursive: true, force: true }); }
});
