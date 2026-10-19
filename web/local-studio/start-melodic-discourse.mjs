// Explicit local-only listening laboratory; production and Cloud stay unchanged.
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '../..');
process.env.PULSO_LOCAL_PORT ||= '4193';
process.env.PULSO_LOCAL_WORKER_PATH ||= path.join(root, 'build-cloud-verify/Release/pulso_cloud_worker.exe');
process.env.PULSO_LOCAL_EXPERIMENTAL = '1';
process.env.PULSO_AI_JOINT_ONE_GROUP = '1';
process.env.PULSO_AI_EXPRESSIVE_ENSEMBLE_EXPERIMENT = '1';
process.env.PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT = '1';
process.env.PULSO_AI_DISABLE_RESPONSE_CACHE = '1';
await import('./server.mjs');
