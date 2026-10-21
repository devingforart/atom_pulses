// Preserve the groundwork anchor; add general, joint development decisions.
process.env.PULSO_LOCAL_PORT ||= '4205';
process.env.PULSO_AI_CONSEQUENTIAL_DEVELOPMENT_EXPERIMENT = '1';
await import('./start-thematic-groundwork.mjs');
