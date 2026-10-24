// Experimental local-only musical reference. This describes accepted MIDI to
// the composer; it never manufactures or moves notes in the generated score.
export function referencePrompt(request, source, blueprint, checkpoint) {
  if (!source || !blueprint || !checkpoint || !Array.isArray(checkpoint.notes))
    throw new Error('La obra de referencia no tiene MIDI IA verificable.');
  if (source.behavior !== request.behavior || source.bpm !== request.bpm ||
      source.allow_chromatic !== request.allow_chromatic)
    throw new Error('La referencia debe conservar enfoque, tempo y política cromática.');
  if (request.duration_seconds <= source.duration_seconds)
    throw new Error('La extensión debe durar más que la obra de referencia.');
  const lead = blueprint.instruments?.find(part => part.source_voice === 'lead');
  const bass = blueprint.instruments?.find(part => part.id === 'moving_bass') ||
    blueprint.instruments?.find(part => part.source_voice === 'movement_bass');
  const pulse = blueprint.instruments?.find(part => part.id === 'chord_pulse') ||
    blueprint.instruments?.find(part => part.source_voice === 'harmonic_pulse');
  if (!lead || !bass || !pulse) throw new Error('La referencia no contiene protagonista, bajo y pulso armónico.');
  const notes = id => checkpoint.notes.filter(note => note.instrument_id === id)
    .sort((a, b) => a.start_beat - b.start_beat);
  const leadNotes = notes(lead.id);
  const bassNotes = notes(bass.id);
  const pulseNotes = notes(pulse.id);
  if (leadNotes.length < 8 || bassNotes.length < 8 || pulseNotes.length < 8)
    throw new Error('La referencia carece de notas suficientes para anclar la extensión.');
  const names = ['C', 'Db', 'D', 'Eb', 'E', 'F', 'Gb', 'G', 'Ab', 'A', 'Bb', 'B'];
  const pitch = value => `${names[value % 12]}${Math.floor(value / 12) - 1}`;
  const melody = leadNotes.slice(0, 8).map(note => pitch(note.pitch)).join('-');
  const attacks = list => [...new Set(list.filter(note => note.start_beat < 16)
    .map(note => Number((note.start_beat % 4).toFixed(2))))].sort((a, b) => a - b).join(',');
  const chordIds = [...new Set((blueprint.sections || []).flatMap(section =>
    (section.harmonic_events || []).map(event => event.chord_id)))].slice(0, 4).join('-');
  const entrance = list => Math.floor(list[0].start_beat / 4) + 1;
  const anchor = `Referencia MIDI aprobada: ${blueprint.key}, ${blueprint.instruments.length} voces, acordes ${chordIds}; ` +
    `sujeto real ${melody}; fases bajo ${attacks(bassNotes)}, acordes ${attacks(pulseNotes)}. ` +
    `Bajo y pulso entran en compás ${entrance(bassNotes)}/${entrance(pulseNotes)}, protagonista en ${entrance(leadNotes)}: no postergues esas entradas. ` +
    'Conserva sujeto y groove. Amplía su historia por actos con contraste, clímax y regreso; no reinicies miniobras.';
  const prompt = `${request.prompt}\n${anchor}`;
  if (prompt.length > 600) throw new Error('El encargo más la referencia MIDI supera el límite de 600 caracteres.');
  return prompt;
}
