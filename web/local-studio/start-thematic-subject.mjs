// AI-authored reference sentence -> complete AI dialogue -> AI accompaniment.
import path from 'node:path';
import { fileURLToPath } from 'node:url';
const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
process.env.PULSO_LOCAL_PORT ||= '4196';
process.env.PULSO_LOCAL_WORKER_PATH ||= path.join(root, 'build-ensemble/Release/pulso_cloud_worker.exe');
process.env.PULSO_LOCAL_EXPERIMENTAL = '1';
process.env.PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT = '1';
process.env.PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT = '1';
process.env.PULSO_AI_DISABLE_RESPONSE_CACHE = '1';
await import('./server.mjs');
