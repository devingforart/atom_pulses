// Explicit local model comparison only; never changes the shared/production default.
process.env.PULSO_LOCAL_PORT ||= '4212';
process.env.PULSO_AI_EXPERIMENT_MODEL = 'gpt-6.1-sol';
delete process.env.PULSO_AI_EXPERIMENT_EFFORT;
await import('./start-gesture-score.mjs');
