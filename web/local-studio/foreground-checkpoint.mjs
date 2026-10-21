// Exact, local research import. No selection by pitch, rhythm or quality.
export function foregroundCheckpoint(source, plan, midi) {
  if (source?.schema_version !== 2 || source.prewritten_dialogue !== true ||
      !Array.isArray(source.cast_ids) || !Array.isArray(plan?.cast) ||
      JSON.stringify(source.cast_ids) !== JSON.stringify(plan.cast.map(part => part.id)))
    throw new Error('The foreground checkpoint does not match its accepted cast.');
  const notes = [];
  const known = new Set();
  for (const track of midi.tracks.filter(track => track.notes.length)) {
    const owners = plan.cast.filter(part => track.name === part.name || track.name === `PULSO ${part.name}`);
    if (owners.length !== 1 || known.has(owners[0].id))
      throw new Error('The foreground checkpoint has ambiguous track identity.');
    known.add(owners[0].id);
    for (const note of track.notes) {
      if (note.unterminated || !Number.isFinite(note.startBeat) || note.startBeat < 0 ||
          !Number.isFinite(note.durationBeats) || note.durationBeats <= 0)
        throw new Error('The foreground MIDI has an invalid note representation.');
      notes.push({ instrument_id: owners[0].id, start_beat: note.startBeat,
        duration_beats: note.durationBeats, pitch: note.pitch, velocity: note.velocity });
    }
  }
  if (!notes.length) throw new Error('The foreground checkpoint is empty.');
  return { ...source, completed_units: 0, prewritten_dialogue: true, notes };
}
