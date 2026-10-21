import test from 'node:test';
import assert from 'node:assert/strict';
import { subjectReturns, selectedForegroundFile } from '../tools/analyze-thematic-subject.mjs';

test('preservation evidence compares the selected AI draft, not an intentionally revised original', () => {
  assert.equal(selectedForegroundFile(null), 'partial-01.mid');
  assert.equal(selectedForegroundFile({status:'AI kept the original melodic draft'}), 'partial-01.mid');
  assert.equal(selectedForegroundFile({status:'original retained; revision representation invalid'}), 'partial-01.mid');
  assert.equal(selectedForegroundFile({status:'AI rehearsal revision accepted; listening comparison remains necessary'}), 'partial-02.mid');
});

test('thematic evidence recognizes complete literal/transposed returns without changing notes', () => {
  const theme = [{ beat: 0, duration: 1, pitch: 62 },
    { beat: 1, duration: 0.5, pitch: 67 }, { beat: 1.5, duration: 1.5, pitch: 65 }];
  const notes = theme.flatMap(note => [
    { startBeat: note.beat, durationBeats: note.duration, pitch: note.pitch },
    { startBeat: 16 + note.beat, durationBeats: note.duration, pitch: note.pitch + 12 },
  ]);
  const before = JSON.stringify({ theme, notes });
  assert.deepEqual(subjectReturns(theme, notes).filter(match => match.completeReference)
    .map(match => [match.startBeat, match.transpose]), [[0, 0], [16, 12]]);
  assert.equal(JSON.stringify({ theme, notes }), before);
});

test('same pitches with different rhythmic identity are not claimed as a complete exact return', () => {
  const theme = [{ beat: 0, duration: 1, pitch: 62 },
    { beat: 1, duration: 1, pitch: 65 }, { beat: 2, duration: 2, pitch: 64 }];
  const notes = [{ startBeat: 0, durationBeats: 1, pitch: 62 },
    { startBeat: 1.5, durationBeats: 1, pitch: 65 },
    { startBeat: 3, durationBeats: 1, pitch: 64 }];
  assert.equal(subjectReturns(theme, notes).some(match => match.completeReference), false);
});
