export function blueprintRequestMatchesSource(source, request, allowEditorialChange = false) {
  return Boolean(source && ['prompt', 'duration_seconds', 'bpm', 'behavior', 'seed',
    'ai_sovereign', 'phrase_architecture_experiment', 'allow_chromatic', 'pitched_only'].every(field => source[field] === request[field]) &&
    (source.first_pass_audition === request.first_pass_audition || allowEditorialChange));
}

export function completedMusicalWindows(resume, behavior) {
  // Match AiComposer's technical writer window, including legacy Club plans.
  const barsPerWindow = behavior === 'club' ? 8 : 16;
  return resume.sections.reduce((sum, section) => sum + Math.ceil(section.bars / barsPerWindow), 0);
}

// A published score is authoritative over an older provisional checkpoint.
// Recover exact exported events, never regenerate or infer missing music.
export function completedMidiResume(midi, plan, manifest, source) {
  if (!Array.isArray(plan?.cast) || !Array.isArray(midi?.tracks) ||
      midi.tracks.length !== plan.cast.length + 1 || midi.tracks[0].notes.length ||
      source?.schema_version !== 2 || !Array.isArray(source.cast_ids) ||
      !Array.isArray(source.sections) || source.sections.length === 0 ||
      JSON.stringify(source.cast_ids) !== JSON.stringify(plan.cast.map(owner => owner.id)) ||
      source.completed_units !== completedMusicalWindows(source, manifest?.composition_behavior)) return null;
  const notes = [];
  for (const [index, owner] of plan.cast.entries()) {
    const track = midi.tracks[index + 1];
    if (![owner.name, `PULSO ${owner.name}`].includes(track.name) || track.notes.some(note => note.unterminated)) return null;
    for (const note of track.notes) notes.push({ instrument_id: owner.id,
      start_beat: note.startBeat, duration_beats: note.durationBeats,
      pitch: note.pitch, velocity: note.velocity });
  }
  if (notes.length !== manifest?.all_midi_notes || notes.length === 0) return null;
  return { ...source, notes };
}

// The cast is the complete instrument roster; checkpoint.parts counts only
// tracks that have become audible so far. Silent future owners remain valid.
export function exactResumeMatchesCheckpoint(checkpoint, resume) {
  return checkpoint?.resumeFile === 'resume-score.json' &&
    resume?.schema_version === 2 &&
    resume.completed_units === checkpoint.completed_units &&
    Array.isArray(resume.notes) && resume.notes.length === checkpoint.notes &&
    Array.isArray(resume.cast_ids) &&
    resume.cast_ids.length >= checkpoint.parts &&
    new Set(resume.cast_ids).size === resume.cast_ids.length &&
    Array.isArray(resume.sections) && resume.sections.length > 0 &&
    typeof resume.blueprint_json === 'string' && resume.blueprint_json.length > 0 &&
    resume.notes.every(note => typeof note.instrument_id === 'string' &&
      resume.cast_ids.includes(note.instrument_id) &&
      Number.isFinite(note.start_beat) && Number.isFinite(note.duration_beats) &&
      Number.isInteger(note.pitch) && Number.isInteger(note.velocity));
}
