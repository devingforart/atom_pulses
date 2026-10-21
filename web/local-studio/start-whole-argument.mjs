// Whole-work melodic argument in one AI response, then scoped rehearsal,
// then accompaniment against immutable selected foreground MIDI.
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
process.env.PULSO_LOCAL_PORT ||= '4206';
process.env.PULSO_LOCAL_WORKER_PATH = path.join(root, 'build-development/Release/pulso_cloud_worker.exe');
process.env.PULSO_LOCAL_EXPERIMENTAL = '1';
for (const key of ['PULSO_AI_WHOLE_ARGUMENT_EXPERIMENT', 'PULSO_AI_THEMATIC_GROUNDWORK_EXPERIMENT',
  'PULSO_AI_PHRASE_DEVELOPMENT_EXPERIMENT', 'PULSO_AI_PHRASE_PROSODY_EXPERIMENT',
  'PULSO_AI_ENSEMBLE_DISCOURSE_EXPERIMENT', 'PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT',
  'PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT', 'PULSO_AI_THEMATIC_REHEARSAL_EXPERIMENT',
  'PULSO_AI_DISABLE_RESPONSE_CACHE']) process.env[key] = '1';
delete process.env.PULSO_AI_JOINT_PITCHED_SCORE_EXPERIMENT;
delete process.env.PULSO_AI_CONSEQUENTIAL_DEVELOPMENT_EXPERIMENT;
delete process.env.PULSO_AI_NARRATIVE_FOCUS;
await import('./server.mjs');
