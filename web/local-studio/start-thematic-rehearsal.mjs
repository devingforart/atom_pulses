// Local A/B only. One explicit AI melodic rehearsal precedes orchestration.
process.env.PULSO_LOCAL_PORT ||= '4197';
process.env.PULSO_AI_THEMATIC_REHEARSAL_EXPERIMENT = '1';
await import('./start-thematic-subject.mjs');
