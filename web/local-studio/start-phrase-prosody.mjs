// General generator experiment, not a song-specific MIDI repair.
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
process.env.PULSO_LOCAL_PORT ||= '4200';
process.env.PULSO_LOCAL_WORKER_PATH = path.join(root, 'build-prosody/Release/pulso_cloud_worker.exe');
process.env.PULSO_LOCAL_EXPERIMENTAL = '1';
process.env.PULSO_AI_PHRASE_PROSODY_EXPERIMENT = '1';
process.env.PULSO_AI_JOINT_PITCHED_SCORE_EXPERIMENT = '1';
process.env.PULSO_AI_ENSEMBLE_DISCOURSE_EXPERIMENT = '1';
process.env.PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT = '1';
process.env.PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT = '1';
process.env.PULSO_AI_DISABLE_RESPONSE_CACHE = '1';
// Do not inherit a previous work's commissioned repair as a global instruction.
delete process.env.PULSO_AI_NARRATIVE_FOCUS;
// Explicit general-generator A/B, never inherit a prior song's commissioned edit.
if (process.argv.includes('--rehearsal'))
  process.env.PULSO_AI_THEMATIC_REHEARSAL_EXPERIMENT = '1';
else
  delete process.env.PULSO_AI_THEMATIC_REHEARSAL_EXPERIMENT;
await import('./server.mjs');
