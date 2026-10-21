import { readFileSync } from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { AUTOMATIC_RECOVERY_MAX_SECONDS, AUTOMATIC_RECOVERY_MAX_USD } from './recovery-policy.mjs';

const here = path.dirname(fileURLToPath(import.meta.url));
export const composerProfile = JSON.parse(readFileSync(
  path.resolve(here, '..', 'shared', 'composer-profile.json'), 'utf8'));
export const experimentalProfile = process.env.PULSO_LOCAL_EXPERIMENTAL === '1';

export function defaultEditorialSettings(input, proofMode, renderMode) {
  const sovereign = !proofMode && renderMode === 'ai_sovereign';
  return {
    firstPass: input.first_pass_audition === undefined
      ? Boolean(composerProfile.defaultFirstPass && sovereign && !input.review_from_job)
      : input.first_pass_audition === true,
    phraseArchitecture: input.phrase_architecture_experiment === undefined
      ? Boolean(composerProfile.defaultPhraseArchitecture && sovereign)
      : input.phrase_architecture_experiment === true,
  };
}

const experimentalKeys = ['PULSO_AI_SINGLE_OWNER_WRITING', 'PULSO_AI_FAST_MODE',
  'PULSO_AI_COMPACT_WIRE', 'PULSO_AI_DENSE_WIRE', 'PULSO_AI_PARALLEL_FRONTIER',
  'PULSO_AI_JOINT_ONE_GROUP', 'PULSO_AI_JOINT_COMPACT_CAST',
  'PULSO_AI_GROUP_SHORT_FOUNDATION', 'PULSO_AI_MELODIC_EXPERIMENT',
  'PULSO_AI_GLOBAL_NARRATIVE_EXPERIMENT', 'PULSO_AI_NARRATIVE_FULL_SCENE',
  'PULSO_AI_RHYTHMIC_ARC_EXPERIMENT', 'PULSO_AI_NARRATIVE_FINALE_EXPERIMENT',
  'PULSO_AI_TERMINAL_CRITIC_EXPERIMENT',
  'PULSO_AI_EXPRESSIVE_ENSEMBLE_EXPERIMENT',
  'PULSO_AI_MELODIC_DISCOURSE_EXPERIMENT',
  'PULSO_AI_ENSEMBLE_DISCOURSE_EXPERIMENT',
  'PULSO_AI_PHRASE_PROSODY_EXPERIMENT',
  'PULSO_AI_PHRASE_DEVELOPMENT_EXPERIMENT',
  'PULSO_AI_THEMATIC_GROUNDWORK_EXPERIMENT',
  'PULSO_AI_CONSEQUENTIAL_DEVELOPMENT_EXPERIMENT',
  'PULSO_AI_WHOLE_ARGUMENT_EXPERIMENT',
  'PULSO_AI_MATERIAL_DIALOGUE_EXPERIMENT',
  'PULSO_AI_EXPECTATION_LED_EXPERIMENT',
  'PULSO_AI_GESTURE_SCORE_EXPERIMENT',
  'PULSO_AI_JOINT_PITCHED_SCORE_EXPERIMENT',
  'PULSO_AI_COMPLETE_DIALOGUE_EXPERIMENT',
  'PULSO_AI_THEMATIC_SUBJECT_EXPERIMENT',
  'PULSO_AI_THEMATIC_REHEARSAL_EXPERIMENT',
  'PULSO_AI_LONG_FORM_CONTINUITY', 'PULSO_AI_LONG_FORM_ARC',
  'PULSO_AI_THEMATIC_REVIEW', 'PULSO_AI_GROUNDED_STORY_EXPERIMENT'];

export function workerEnvironmentFor(source, directory, dataRoot, automatic = false,
    experimental = experimentalProfile, firstPassAudition = false, reviewAudition = false,
    phraseArchitectureExperiment = false) {
  const systemKeys = ['OPENAI_API_KEY', 'PATH', 'Path', 'SystemRoot', 'WINDIR',
    'TEMP', 'TMP', 'USERPROFILE', 'APPDATA', 'LOCALAPPDATA', 'ProgramData'];
  return { ...Object.fromEntries(systemKeys.filter(key => source[key]).map(key => [key, source[key]])),
    ...composerProfile.workerEnvironment,
    PULSO_TRACE_PATH: path.join(directory, 'api-events.jsonl'),
    ...(firstPassAudition ? { PULSO_AI_FIRST_PASS_AUDITION: '1' } : {}),
    ...(reviewAudition ? { PULSO_AI_REVIEW_AUDITION: '1' } : {}),
    ...(phraseArchitectureExperiment ? { PULSO_AI_PHRASE_ARCHITECTURE_EXPERIMENT: '1' } : {}),
    ...(experimental ? Object.fromEntries(experimentalKeys.filter(key => source[key] === '1').map(key => [key, '1'])) : {}),
    ...(experimental && typeof source.PULSO_AI_NARRATIVE_FOCUS === 'string' &&
      source.PULSO_AI_NARRATIVE_FOCUS.length <= 2000 ?
      { PULSO_AI_NARRATIVE_FOCUS: source.PULSO_AI_NARRATIVE_FOCUS } : {}),
    ...(experimental && !firstPassAudition && source.PULSO_AI_DISABLE_RESPONSE_CACHE !== '1' ? { PULSO_AI_RESPONSE_CACHE_DIR: path.join(dataRoot, 'validated-response-cache') } : {}),
    ...(experimental && source.PULSO_AI_EXPERIMENT_EFFORT === 'low' ? { PULSO_AI_EXPERIMENT_EFFORT: 'low' } : {}),
    ...(experimental && ['gpt-6.1-sol', 'gpt-6-luna'].includes(source.PULSO_AI_EXPERIMENT_MODEL) ? { PULSO_AI_EXPERIMENT_MODEL: source.PULSO_AI_EXPERIMENT_MODEL } : {}),
    ...(automatic ? { PULSO_AUTO_RECOVERY_MAX_SECONDS: String(AUTOMATIC_RECOVERY_MAX_SECONDS), PULSO_AUTO_RECOVERY_MAX_USD: String(AUTOMATIC_RECOVERY_MAX_USD) } : {}) };
}
