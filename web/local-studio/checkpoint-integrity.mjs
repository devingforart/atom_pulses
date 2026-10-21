export function blueprintRequestMatchesSource(source, request, allowEditorialChange = false) {
  return Boolean(source && ['prompt', 'duration_seconds', 'bpm', 'behavior', 'seed',
    'ai_sovereign', 'phrase_architecture_experiment'].every(field => source[field] === request[field]) &&
    (source.first_pass_audition === request.first_pass_audition || allowEditorialChange));
}

// The cast is the complete instrument roster; checkpoint.parts counts only
// tracks that have become audible so far. Silent future owners remain valid.
export function exactResumeMatchesCheckpoint(checkpoint, resume) {
  return checkpoint?.resumeFile === 'resume-score.json' &&
    resume?.schema_version === 2 &&
    Number.isInteger(resume.completed_units) && resume.completed_units >= 0 &&
    (resume.completed_units > 0 || resume.prewritten_dialogue === true) &&
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
