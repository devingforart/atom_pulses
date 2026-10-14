import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { AUTOMATIC_RECOVERY_MAX_SECONDS, AUTOMATIC_RECOVERY_MAX_USD } from './recovery-policy.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
export const composerProfile = JSON.parse(readFileSync(
  path.resolve(here, '..', 'shared', 'composer-profile.json'), 'utf8'));
export const experimentalProfile = process.env.PULSO_LOCAL_EXPERIMENTAL === '1';

const experimentalKeys = ['PULSO_AI_SINGLE_OWNER_WRITING', 'PULSO_AI_FAST_MODE',
  'PULSO_AI_COMPACT_WIRE', 'PULSO_AI_DENSE_WIRE', 'PULSO_AI_PARALLEL_FRONTIER',
  'PULSO_AI_JOINT_ONE_GROUP', 'PULSO_AI_JOINT_COMPACT_CAST',
  'PULSO_AI_GROUP_SHORT_FOUNDATION', 'PULSO_AI_MELODIC_EXPERIMENT',
  'PULSO_AI_GLOBAL_NARRATIVE_EXPERIMENT', 'PULSO_AI_NARRATIVE_FULL_SCENE'];

export function workerEnvironmentFor(source, directory, dataRoot, automatic = false,
    experimental = experimentalProfile) {
  const systemKeys = ['OPENAI_API_KEY', 'PATH', 'Path', 'SystemRoot', 'WINDIR',
    'TEMP', 'TMP', 'USERPROFILE', 'APPDATA', 'LOCALAPPDATA', 'ProgramData'];
  return { ...Object.fromEntries(systemKeys.filter(key => source[key]).map(key => [key, source[key]])),
    ...composerProfile.workerEnvironment,
    PULSO_TRACE_PATH: path.join(directory, 'api-events.jsonl'),
    ...(experimental ? Object.fromEntries(experimentalKeys.filter(key => source[key] === '1').map(key => [key, '1'])) : {}),
    ...(experimental && typeof source.PULSO_AI_NARRATIVE_FOCUS === 'string' &&
      source.PULSO_AI_NARRATIVE_FOCUS.length <= 2000 ?
      { PULSO_AI_NARRATIVE_FOCUS: source.PULSO_AI_NARRATIVE_FOCUS } : {}),
    ...(experimental && source.PULSO_AI_DISABLE_RESPONSE_CACHE !== '1' ? { PULSO_AI_RESPONSE_CACHE_DIR: path.join(dataRoot, 'validated-response-cache') } : {}),
    ...(experimental && source.PULSO_AI_EXPERIMENT_EFFORT === 'low' ? { PULSO_AI_EXPERIMENT_EFFORT: 'low' } : {}),
    ...(experimental && ['gpt-6.1-sol', 'gpt-6-luna'].includes(source.PULSO_AI_EXPERIMENT_MODEL) ? { PULSO_AI_EXPERIMENT_MODEL: source.PULSO_AI_EXPERIMENT_MODEL } : {}),
    ...(automatic ? { PULSO_AUTO_RECOVERY_MAX_SECONDS: String(AUTOMATIC_RECOVERY_MAX_SECONDS), PULSO_AUTO_RECOVERY_MAX_USD: String(AUTOMATIC_RECOVERY_MAX_USD) } : {}) };
}
