// Isolated notes-first experiment: concise whole-score direction, not more prose fields.
process.env.PULSO_LOCAL_PORT ||= '4209';
process.env.PULSO_AI_EXPECTATION_LED_EXPERIMENT = '1';
delete process.env.PULSO_AI_MATERIAL_DIALOGUE_EXPERIMENT;
await import('./start-whole-argument.mjs');
