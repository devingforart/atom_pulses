import { parseMidi } from './midi.mjs';
import { SuiteAudio, instrumentFor } from './audio-engine.mjs';

const $ = selector => document.querySelector(selector);
// A short, affordable proof of musical coherence is the safest default while
// the full-length writer is being evaluated. At 120 BPM, 32 s is ~16 bars.
$('#duration').add(new Option('Prueba corta · ~16 compases (120 BPM)', '32', true, true), 0);
const ui = { form: $('#compose-form'), prompt: $('#prompt'), submit: $('#submit'), notice: $('#notice'), health: $('#health'), jobs: $('#jobs'), session: $('#session-content'), timeline: $('#timeline'), play: $('#play'), stop: $('#stop'), back: $('#back'), export: $('#export-all'), history: $('#history-panel') };
ui.prompt.placeholder = 'Prueba corta: una melodía protagonista, un bajo y acordes que evolucionan con una resolución clara.';
const proofLabel = document.createElement('label');
proofLabel.className = 'proof-option';
const proofInput = document.createElement('input');
proofInput.type = 'checkbox'; proofInput.id = 'proof-mode'; proofInput.checked = true;
const proofText = document.createElement('span');
proofText.textContent = 'Prueba de coherencia · 3 pistas: acordes, bajo y melodía';
proofLabel.append(proofInput, proofText);
$('#compose-form .field-pair').after(proofLabel);
const syncProofMode = () => {
  proofInput.disabled = Number($('#duration').value) > 60 || $('#render-mode').value !== 'ai_sovereign';
  if (proofInput.disabled) proofInput.checked = false;
  proofLabel.classList.toggle('disabled', proofInput.disabled);
};
$('#duration').addEventListener('change', syncProofMode);
$('#render-mode').addEventListener('change', syncProofMode);
syncProofMode();
const states = { queued: 'EN COLA', running: 'COMPONIENDO', completed: 'LISTA', failed: 'FALLIDA', interrupted: 'INTERRUMPIDA', cancelled: 'CANCELADA' };
const palette = { pad: '#dfb64e', bass: '#71a1d0', keys: '#8b9dca', pluck: '#cc8b6e', lead: '#d84f2b', drums: '#75b57c', synth: '#a899c7' };
const names = { pad: 'Seno · armonía', bass: 'Seno · bajo', keys: 'Seno · teclas', pluck: 'Seno · arpegio', lead: 'Seno · melodía', drums: 'Seno · percusión', synth: 'Seno · pista' };
const fmt = seconds => { const value = Math.max(0, Math.floor(seconds || 0)); return `${String(Math.floor(value / 60)).padStart(2, '0')}:${String(value % 60).padStart(2, '0')}`; };
const node = (tag, className, value) => { const el = document.createElement(tag); if (className) el.className = className; if (value !== undefined) el.textContent = value; return el; };
const fileUrl = (job, filename) => `/api/jobs/${job.id}/files/${encodeURIComponent(filename)}`;
const download = (job, filename, label, className = '') => { const a = node('a', className, label); a.href = fileUrl(job, filename); a.download = filename; return a; };
const number = value => new Intl.NumberFormat('es-AR').format(Number(value) || 0);
let jobs = [], selectedId = null, loadedKey = null, loadVersion = 0, current = null, barPx = 22, playhead = null, previewDiagnostic = false;
const audio = new SuiteAudio(updatePosition, () => { ui.play.textContent = '▶'; ui.play.setAttribute('aria-label', 'Reproducir'); });

async function api(url, options = {}) { const response = await fetch(url, { cache: 'no-store', ...options }); const result = await response.json(); if (!response.ok) throw new Error(result.error || `Error HTTP ${response.status}`); return result; }
function showNotice(message) { ui.notice.textContent = message || ''; ui.notice.hidden = !message; }
function setExport(job, filename) {
  if (filename) { ui.export.classList.remove('disabled'); ui.export.removeAttribute('aria-disabled'); ui.export.href = fileUrl(job, filename); ui.export.download = filename; }
  else { ui.export.classList.add('disabled'); ui.export.setAttribute('aria-disabled', 'true'); ui.export.removeAttribute('href'); ui.export.removeAttribute('download'); }
}
function updatePosition(seconds) {
  $('#time-display').textContent = `${fmt(seconds)} / ${fmt(audio.duration)}`;
  if (playhead && current) {
    const beat = current.midi.secondsToBeat(seconds);
    const x = 205 + beat / current.midi.beatsPerBar * barPx;
    playhead.style.left = `${x}px`;
    if (audio.playing && x - ui.timeline.scrollLeft > ui.timeline.clientWidth - 70)
      ui.timeline.scrollLeft = Math.max(0, x - Math.max(280, ui.timeline.clientWidth * 0.55));
  }
}
function actionButton(label, callback, className = '') { const button = node('button', className, label); button.type = 'button'; button.addEventListener('click', callback); return button; }
function reuse(job) {
  ui.prompt.value = job.request.prompt;
  $('#duration').value = String(job.request.duration_seconds);
  proofInput.checked = Boolean(job.request.proof_mode);
  syncProofMode();
  $('#bpm').value = String(job.request.bpm);
  $('#behavior').value = job.request.behavior || 'adaptive';
  $('#render-mode').value = job.request.ai_sovereign ? 'ai_sovereign' : 'standard';
  syncProofMode();
  $('#seed').value = job.request.seed;
  $('#prompt-count').textContent = `${ui.prompt.value.length} / 600`;
  ui.history.hidden = true;
  showNotice('Ajustes cargados. Revisa antes de componer: todavía no se envió ninguna solicitud.');
  ui.prompt.focus();
}
function renderSession(job) {
  if (!job) return;
  ui.session.replaceChildren();
  ui.session.append(node('h3', 'session-title', job.manifest?.title || 'Composición en curso'));
  ui.session.append(node('div', `session-status ${job.state}`, states[job.state] || job.state.toUpperCase()));
  ui.session.append(node('p', 'session-prompt', job.request.prompt));
  const date = new Date(job.request.created_at).toLocaleString('es-AR', { dateStyle: 'short', timeStyle: 'short' });
  ui.session.append(node('p', 'session-meta', `${date} · ${fmt(job.request.duration_seconds)} · ${job.request.bpm} BPM · semilla ${job.request.seed}`));
  if (job.request.editorial_profile === 'ai-only-v2') ui.session.append(node('p', 'session-note', 'Perfil editorial local IA v2 · cada nota es autoría IA.'));
  if (job.state === 'running' || job.state === 'queued') {
    const elapsed = Math.max(0, (Date.now() - Date.parse(job.request.created_at)) / 1000);
    ui.session.append(node('p', 'session-note', `${job.stage || 'Iniciando'} · ${fmt(elapsed)}${job.total ? ` · ${job.completed}/${job.total}` : ''}`));
    const progress = node('div', 'progress-track'), fill = node('i'); fill.style.width = job.total ? `${Math.min(100, 100 * job.completed / job.total)}%` : '12%'; progress.append(fill); ui.session.append(progress);
  }
  if (job.error) ui.session.append(node('p', 'notice', job.error));
  if (job.checkpoint?.file && !job.manifest) ui.session.append(node('p', 'session-note', `Hay MIDI provisional: ${job.checkpoint.parts || 0} pistas con notas y ${job.checkpoint.notes || 0} notas. No es una obra terminada ni auditada.`));
  if (job.manifest?.editorial) {
    const q = job.manifest.editorial, ready = q.creative_ready && q.narrative_ready && q.soundscape_ready && q.track_viability_ready && !q.musical_review_required;
    const box = node('div', `editorial ${ready ? 'complete' : ''}`);
    box.append(node('strong', '', ready ? 'Partitura lista para audición' : 'Partitura exportable · revisión musical pendiente'));
    box.append(node('span', '', `Resolución ${Math.round((q.resolution_score || 0) * 100)}% · diálogos ${q.dialogue_lines ?? 0} · compases bajo objetivo ${q.underfilled_bars ?? 0}`));
    if (!q.technical_ready) box.append(node('div', '', 'La integridad técnica MIDI requiere revisión.'));
    if (q.musical_review_required) box.append(node('div', '', `Revisión musical sugerida: ${q.harmonic_conflicts ?? 0} tensiones verticales y ${q.low_register_conflicts ?? 0} cruces graves. El MIDI es íntegro; escuchá el contexto antes de decidir.`));
    if (q.review_attempted) box.append(node('div', '', q.review_accepted
      ? `La IA revisó ${q.review_windows ?? 0} pasajes; riesgo contextual ${Number(q.contextual_risk_before ?? 0).toFixed(1)} → ${Number(q.contextual_risk_after ?? 0).toFixed(1)}. Las demás notas quedaron intactas.`
      : `La IA revisó ${q.review_windows ?? 0} pasajes, pero se conservó la partitura original porque la alternativa no mejoró con seguridad.`));
    if (q.underwritten_roles_advisory) box.append(node('div', '', `Alguna función musical quedó poco desarrollada; el colchón presenta acordes polifónicos en ${q.chord_bed_polyphonic_stages ?? 0} de 3 tramos.`));
    if (q.uniform_activity_advisory) box.append(node('div', '', 'Las entradas de melodía y acordes se repiten con la misma frecuencia en cada compás: escuchá si la hipnosis evoluciona lo suficiente.'));
    ui.session.append(box);
  }
  const actions = node('div', 'session-actions');
  actions.append(actionButton('Repetir ajustes', () => reuse(job)));
  if (job.manifest?.fullFile) actions.append(download(job, job.manifest.fullFile, '↓ Obra completa MIDI', 'primary-link'));
  if (job.checkpoint?.file && !job.manifest) actions.append(download(job, job.checkpoint.file, '↓ MIDI provisional'));
  if (job.diagnostic?.file && !job.manifest) actions.append(download(job, job.diagnostic.file, '↓ MIDI rechazado (diagnóstico)'));
  if (job.diagnostic?.planFile && !job.manifest) actions.append(download(job, job.diagnostic.planFile, 'Plan del candidato'));
  if (job.diagnostic?.auditFile && !job.manifest) actions.append(download(job, job.diagnostic.auditFile, 'Auditoría del candidato'));
  if (job.diagnostic?.file && !job.manifest) actions.append(actionButton(previewDiagnostic ? 'Escuchar último aceptado' : 'Escuchar candidato rechazado', () => {
    previewDiagnostic = !previewDiagnostic; loadedKey = null; showSelected();
  }));
  if (job.manifest?.comparisonFile) actions.append(download(job, job.manifest.comparisonFile, 'Referencia A/B'));
  if (job.manifest?.planFile) actions.append(download(job, job.manifest.planFile, 'Plan compositivo'));
  if (job.manifest?.auditFile) actions.append(download(job, job.manifest.auditFile, 'Auditoría musical'));
  if (job.manifest) actions.append(download(job, 'manifest.json', 'Manifiesto'));
  actions.append(download(job, 'job.json', 'Solicitud JSON'));
  const trace = node('a', '', '↓ Traza JSON'); trace.href = `/api/jobs/${job.id}/trace?download=1`; trace.download = `pulso-trace-${job.id}.json`; actions.append(trace);
  if (job.state === 'running' || job.state === 'queued') actions.append(actionButton('Detener proceso', async () => {
    if (!confirm('¿Detener la composición local? Una solicitud remota ya aceptada podría seguir consumiendo créditos.')) return;
    try { const result = await api(`/api/jobs/${job.id}/cancel`, { method: 'POST', headers: { 'X-Pulso-Local': '1' } }); showNotice(result.message); await refresh(); } catch (error) { showNotice(error.message); }
  }, 'cancel'));
  ui.session.append(actions);
  if (job.telemetry) {
    const t = job.telemetry, stats = node('div', 'telemetry'); stats.append(node('div', 'telemetry-title', 'TRAZABILIDAD'));
    const grid = node('div', 'telemetry-grid');
    const items = [['Modelo', t.model ? `${t.model} / ${t.effort || '—'}` : 'sin traza'], ['Tiempo real', fmt(t.wallMs / 1000)], ['Llamadas IA', String(t.calls)], ['Tokens entrada', number(t.inputTokens)], ['Tokens salida', number(t.outputTokens)], ['Recuperaciones', String(t.recoveryEvents)], ['Costo estimado', t.pricedCalls ? `USD ${Number(t.estimatedUsd).toFixed(4)}` : 'sin datos'], ['Respuestas incompletas', String(t.incompleteCalls)]];
    for (const [label, value] of items) { const cell = node('div', 'telemetry-item'); cell.append(node('span', '', label), node('strong', '', value)); grid.append(cell); }
    stats.append(grid); ui.session.append(stats);
    if (t.pricedCalls) ui.session.append(node('p', 'session-note', 'Costo orientativo; puede excluir llamadas sin uso reportado.'));
  }
}
function renderHistory() {
  ui.jobs.replaceChildren();
  if (!jobs.length) { ui.jobs.append(node('p', 'session-note', 'Todavía no hay obras guardadas.')); return; }
  for (const job of jobs) {
    const button = node('button', `history-job ${selectedId === job.id ? 'selected' : ''}`);
    button.type = 'button';
    button.append(node('strong', '', job.manifest?.title || job.request.prompt.slice(0, 68)));
    button.append(node('span', 'status ' + job.state, states[job.state] || job.state));
    button.append(node('span', '', `${new Date(job.request.created_at).toLocaleString('es-AR', { dateStyle: 'short', timeStyle: 'short' })} · ${fmt(job.request.duration_seconds)} · ${job.manifest?.tracks?.length || job.checkpoint?.parts || 0} pistas`));
    button.addEventListener('click', () => { selectedId = job.id; loadedKey = null; previewDiagnostic = false; ui.history.hidden = true; showSelected(); renderHistory(); });
    ui.jobs.append(button);
  }
}
async function readMidi(job, filename) {
  const response = await fetch(fileUrl(job, filename), { cache: 'no-store' });
  if (!response.ok) throw new Error(`No se pudo leer ${filename} (HTTP ${response.status})`);
  return parseMidi(await response.arrayBuffer());
}
function mergedMidi(parts, manifest) {
  const timing = parts[0].midi;
  return { ...timing, lengthBeats: Math.max(Number(manifest?.bars || 0) * timing.beatsPerBar, ...parts.map(part => part.midi.lengthBeats)) };
}
async function loadSong(job, diagnostic = false) {
  const version = ++loadVersion;
  audio.stop(); current = null; playhead = null;
  ui.play.disabled = true;
  ui.timeline.replaceChildren(node('div', 'timeline-empty', 'Leyendo las notas MIDI de esta obra…'));
  $('#song-title').textContent = diagnostic ? 'MIDI rechazado - diagnóstico' : job.manifest?.title || 'MIDI provisional';
  $('#song-subtitle').textContent = 'Preparando partitura y monitor senoidal…';
  const sourceTracks = diagnostic && job.diagnostic?.file ? [{ filename: job.diagnostic.file, name: 'MIDI rechazado' }] : job.manifest?.tracks?.length ? job.manifest.tracks : job.checkpoint?.file ? [{ filename: job.checkpoint.file, name: 'MIDI provisional' }] : [];
  if (!sourceTracks.length) { ui.timeline.replaceChildren(node('div', 'timeline-empty', 'Esta sesión todavía no tiene MIDI para escuchar.')); $('#song-subtitle').textContent = job.stage || job.error || 'Esperando una obra.'; setExport(job, null); return; }
  try {
    const parts = [];
    // Bounded batches keep large orchestras responsive without flooding the local server.
    for (let start = 0; start < sourceTracks.length; start += 6) {
      const batch = await Promise.all(sourceTracks.slice(start, start + 6).map(async (track, offset) => ({ track, midi: await readMidi(job, track.filename), index: start + offset })));
      if (version !== loadVersion) return;
      parts.push(...batch);
    }
    const timing = mergedMidi(parts, job.manifest);
    const tracks = [];
    for (const part of parts) {
      if (!job.manifest) {
        part.midi.tracks.forEach((track, index) => { if (track.notes.length) tracks.push({ name: track.name || `Pista ${index + 1}`, notes: track.notes, filename: part.track.filename, instrument: instrumentFor({}, track.name) }); });
      } else {
        const notes = part.midi.tracks.flatMap(track => track.notes).sort((a, b) => a.startBeat - b.startBeat);
        if (notes.length) tracks.push({ name: part.track.name, notes, filename: part.track.filename, instrument: instrumentFor(part.track.instrument || {}, part.track.name), meta: part.track.instrument || {} });
      }
    }
    if (!tracks.length) throw new Error('El MIDI no contiene notas reproducibles.');
    current = { job, midi: timing, tracks };
    audio.setSong(timing, tracks);
    renderTimeline();
    $('#song-title').textContent = diagnostic ? 'MIDI rechazado - diagnóstico' : job.manifest?.title || 'MIDI provisional';
    $('#song-subtitle').textContent = diagnostic ? `Candidato rechazado · ${tracks.length} pistas con notas · NO aprobado` : job.manifest ? `${job.manifest.key || 'Tonalidad no declarada'} · ${job.manifest.bars || Math.ceil(timing.lengthBeats / timing.beatsPerBar)} compases · ${tracks.length} pistas con notas` : `Escucha provisional · ${tracks.length} pistas con notas · sin aprobación musical final`;
    $('#song-bpm').textContent = `${Math.round(job.manifest?.bpm || 60e6 / timing.tempos[0].microseconds)} BPM`;
    $('#song-key').textContent = job.manifest?.key || 'MIDI provisional';
    $('#song-stat').textContent = `${number(tracks.reduce((sum, track) => sum + track.notes.length, 0))} NOTAS · ${tracks.length} PISTAS`;
    $('#track-summary').textContent = `${tracks.length} pistas · ${fmt(audio.duration)}`;
    ui.play.disabled = false;
    setExport(job, diagnostic ? job.diagnostic?.file : job.manifest?.fullFile || job.checkpoint?.file);
    updatePosition(0);
  } catch (error) {
    if (version !== loadVersion) return;
    ui.timeline.replaceChildren(node('div', 'timeline-empty', `No se pudo abrir el MIDI: ${error.message}`));
    $('#song-subtitle').textContent = 'La exportación MIDI sigue disponible si el archivo existe.';
    setExport(job, diagnostic ? job.diagnostic?.file : job.manifest?.fullFile || job.checkpoint?.file);
  }
}
function renderTimeline() {
  const { midi, tracks, job } = current;
  const bars = Math.ceil(midi.lengthBeats / midi.beatsPerBar), width = Math.max(620, bars * barPx);
  ui.timeline.classList.remove('empty-timeline'); ui.timeline.replaceChildren();
  const content = node('div', 'timeline-content'); content.style.width = `${205 + width}px`;
  const ruler = node('div', 'ruler-row'), rulerLabel = node('div', 'ruler-label', `${bars} COMPASES`), rulerCanvas = node('div', 'ruler-canvas'); rulerCanvas.style.width = `${width}px`;
  const interval = bars > 240 ? 16 : bars > 80 ? 8 : 4;
  for (let bar = 0; bar <= bars; bar += interval) { const mark = node('span', 'ruler-mark', String(bar + 1)); mark.style.left = `${bar * barPx}px`; rulerCanvas.append(mark); }
  rulerCanvas.addEventListener('click', event => audio.seek(midi.beatToSeconds(((event.clientX - rulerCanvas.getBoundingClientRect().left) / barPx) * midi.beatsPerBar)));
  ruler.append(rulerLabel, rulerCanvas); content.append(ruler);
  tracks.forEach((track, index) => {
    const color = palette[track.instrument] || palette.synth;
    const row = node('div', 'track-row'), label = node('div', 'track-label'); label.style.setProperty('--track-color', color);
    const name = node('div', 'track-name'), dot = node('span', 'track-dot'), title = node('b', '', track.name); title.title = track.name; name.append(dot, title);
    const bottom = node('div', 'track-bottom'), mute = actionButton('M', () => { const value = !audio.muted.has(index); audio.setMute(index, value); mute.classList.toggle('active', value); mute.setAttribute('aria-pressed', String(value)); }), solo = actionButton('S', () => { const value = !audio.soloed.has(index); audio.setSolo(index, value); solo.classList.toggle('active', value); solo.setAttribute('aria-pressed', String(value)); });
    mute.title = `Silenciar ${track.name}`; solo.title = `Escuchar solo ${track.name}`; mute.setAttribute('aria-label', mute.title); solo.setAttribute('aria-label', solo.title); mute.setAttribute('aria-pressed', 'false'); solo.setAttribute('aria-pressed', 'false');
    bottom.append(mute, solo, node('small', '', names[track.instrument] || names.synth));
    const link = download(job, track.filename, '↓', 'track-download'); link.title = `Descargar MIDI de ${track.name}`; link.setAttribute('aria-label', link.title); bottom.append(link);
    label.append(name, bottom);
    const lane = node('div', 'lane'); lane.style.width = `${width}px`;
    const canvas = node('canvas', 'note-canvas'); canvas.style.width = `${width}px`;
    const dpr = Math.min(2, window.devicePixelRatio || 1); canvas.width = Math.ceil(width * dpr); canvas.height = Math.ceil(74 * dpr);
    const context = canvas.getContext('2d'); context.scale(dpr, dpr);
    let min = 127, max = 0;
    for (const note of track.notes) { min = Math.min(min, note.pitch); max = Math.max(max, note.pitch); }
    const span = Math.max(12, max - min + 1);
    context.fillStyle = color;
    for (const note of track.notes) {
      const x = note.startBeat / midi.beatsPerBar * barPx, noteWidth = Math.max(2, note.durationBeats / midi.beatsPerBar * barPx);
      const y = 62 - (note.pitch - min) / span * 48;
      context.globalAlpha = 0.38 + 0.55 * note.velocity / 127;
      context.fillRect(x, y, noteWidth, track.instrument === 'drums' ? 4 : 5);
    }
    context.globalAlpha = 1;
    lane.append(canvas);
    lane.addEventListener('click', event => { const rect = lane.getBoundingClientRect(); audio.seek(midi.beatToSeconds((event.clientX - rect.left) / barPx * midi.beatsPerBar)); });
    row.append(label, lane); content.append(row);
  });
  playhead = node('div', 'playhead'); playhead.style.left = '205px'; content.append(playhead);
  ui.timeline.append(content);
}
function showSelected() {
  const job = jobs.find(item => item.id === selectedId);
  if (!job) return;
  renderSession(job);
  const key = `${job.id}:${previewDiagnostic && job.diagnostic?.file ? job.diagnostic.file : job.manifest?.fullFile || job.checkpoint?.file || job.state}`;
  if (key !== loadedKey) { loadedKey = key; loadSong(job, previewDiagnostic && Boolean(job.diagnostic?.file)); }
}
async function refresh() {
  try {
    const [status, listing] = await Promise.all([api('/api/status'), api('/api/jobs')]);
    jobs = listing.jobs;
    const ready = status.workerReady && status.apiKeyConfigured;
    ui.health.className = `health ${ready ? '' : 'warn'}`;
    ui.health.textContent = ready ? `Perfil editorial local ${status.editorialProfile === 'ai-only-v2' ? 'IA v2' : 'anterior'} listo · Escuchar obras guardadas no consume API.` : `Composición no disponible: ${status.workerReady ? '' : 'falta el generador. '}${status.apiKeyConfigured ? '' : 'falta OPENAI_API_KEY en este proceso.'} Las obras guardadas siguen disponibles.`;
    ui.submit.disabled = Boolean(status.activeJobId) || !ready;
    $('#job-count').textContent = `${jobs.length} OBRAS`;
    if (!jobs.length) {
      if (selectedId || current) {
        audio.stop(); current = null; playhead = null; loadedKey = null; ++loadVersion;
        ui.play.disabled = true; setExport(null, null);
        ui.session.replaceChildren(node('div', 'empty-session', 'El historial está vacío. Comienza una nueva obra para escucharla aquí.'));
        ui.timeline.replaceChildren(node('div', 'timeline-empty', 'Todavía no hay MIDI en el historial local.'));
        $('#song-title').textContent = 'El estudio está listo';
        $('#song-subtitle').textContent = 'Compón una obra para ver y escuchar sus pistas.';
        $('#song-bpm').textContent = '— BPM'; $('#song-key').textContent = '—';
        $('#song-stat').textContent = 'MIDI REAL · MONITOR SENOIDAL';
        $('#track-summary').textContent = '0 pistas';
      }
      selectedId = null; renderHistory(); return;
    }
    if (!selectedId || !jobs.some(job => job.id === selectedId))
      selectedId = (jobs.find(job => job.state === 'running') || jobs.find(job => job.manifest?.tracks?.length) || jobs.find(job => job.checkpoint?.file) || jobs[0])?.id || null;
    renderHistory(); showSelected();
  } catch (error) { ui.health.className = 'health warn'; ui.health.textContent = `No se pudo conectar con el estudio local: ${error.message}`; ui.submit.disabled = true; }
}
ui.form.addEventListener('submit', async event => {
  event.preventDefault(); showNotice(''); ui.submit.disabled = true;
  const payload = { prompt: ui.prompt.value, duration_seconds: Number($('#duration').value), bpm: Number($('#bpm').value), behavior: $('#behavior').value, render_mode: $('#render-mode').value, seed: $('#seed').value.trim(), proof_mode: proofInput.checked && !proofInput.disabled };
  try { const result = await api('/api/jobs', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Pulso-Local': '1' }, body: JSON.stringify(payload) }); selectedId = result.job.id; loadedKey = null; previewDiagnostic = false; await refresh(); }
  catch (error) { showNotice(error.message); ui.submit.disabled = false; }
});
ui.prompt.addEventListener('input', () => { $('#prompt-count').textContent = `${ui.prompt.value.length} / 600`; });
$('#history-toggle').addEventListener('click', () => { ui.history.hidden = !ui.history.hidden; });
$('#history-close').addEventListener('click', () => { ui.history.hidden = true; });
$('#refresh').addEventListener('click', refresh);
ui.play.addEventListener('click', async () => { try { if (audio.playing) audio.pause(); else await audio.play(); ui.play.textContent = audio.playing ? 'Ⅱ' : '▶'; ui.play.setAttribute('aria-label', audio.playing ? 'Pausar' : 'Reproducir'); } catch (error) { showNotice(`No se pudo iniciar el audio: ${error.message}`); } });
ui.stop.addEventListener('click', () => audio.stop());
ui.back.addEventListener('click', () => audio.seek(0));
$('#volume').addEventListener('input', event => audio.setVolume(Number(event.target.value) / 100));
document.addEventListener('visibilitychange', () => { if (document.hidden && audio.playing) { audio.pause(); ui.play.textContent = '▶'; } });
refresh(); setInterval(refresh, 4000);
