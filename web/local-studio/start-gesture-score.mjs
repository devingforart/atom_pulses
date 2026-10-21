// AI-authored, explicitly performed gestures; no expansion of MIDI patterns.
process.env.PULSO_LOCAL_PORT ||= '4210';
process.env.PULSO_AI_GESTURE_SCORE_EXPERIMENT = '1';
await import('./start-expectation-led.mjs');
