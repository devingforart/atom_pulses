import { parseMidi, downloadSingleTrackMidi } from './midi.mjs';
import { instrumentFor } from './audio-engine.mjs';
import { ProductionAudio } from './production-audio.mjs';
import { PATCHES, SOUND_BANK_VERSION, patchOptions, patchForTrack } from './sound-palette.mjs';

const $ = selector => document.querySelector(selector);
const themeToggle = $('#theme-toggle');
try { document.documentElement.dataset.theme = localStorage.getItem('pulso-studio-theme') === 'light' ? 'light' : 'dark'; }
catch { document.documentElement.dataset.theme = 'dark'; }
function updateThemeButton() {
  const light = document.documentElement.dataset.theme === 'light';
  themeToggle.textContent = light ? 'Light / Dark' : 'Dark / Light';
  themeToggle.setAttribute('aria-pressed', String(light));
  themeToggle.setAttribute('aria-label', light ? 'Activar tema oscuro' : 'Activar tema claro');
  themeToggle.title = light ? 'Cambiar a Dark' : 'Cambiar a Light';
}
themeToggle.addEventListener('click', () => {
  const next = document.documentElement.dataset.theme === 'light' ? 'dark' : 'light';
  document.documentElement.dataset.theme = next;
  try { localStorage.setItem('pulso-studio-theme', next); } catch {}
  updateThemeButton();
});
updateThemeButton();
document.querySelector('.suite').classList.add('studio-rail');
const exhibitionId = new URLSearchParams(location.search).get('share');
const exhibitionMode = exhibitionId !== null;
const comparisonId = new URLSearchParams(location.search).get('compare');
const ui = { form: $('#compose-form'), prompt: $('#prompt'), submit: $('#submit'), notice: $('#notice'), health: $('#health'), jobs: $('#jobs'), session: $('#session-content'), timeline: $('#timeline'), play: $('#play'), stop: $('#stop'), back: $('#back'), export: $('#export-all'), exportOne: $('#export-one'), history: $('#history-panel') };
ui.prompt.placeholder = 'Describí la obra, su energía, el recorrido emocional y lo que debería cambiar hasta el final.';
const proofLabel = document.createElement('label');
proofLabel.className = 'proof-option';
const proofInput = document.createElement('input');
proofInput.type = 'checkbox'; proofInput.id = 'proof-mode'; proofInput.checked = false;
const proofText = document.createElement('span');
proofText.textContent = 'Prueba de coherencia · 3 pistas: acordes, bajo y melodía';
proofLabel.append(proofInput, proofText);
$('#compose-form .field-pair').after(proofLabel);
const firstPassLabel = document.createElement('label');
firstPassLabel.className = 'proof-option';
firstPassLabel.title = 'Activada: conservar la primera interpretación MIDI técnicamente válida. Desactivada: permitir la revisión musical IA. El plan de frases sigue activo en ambos casos.';
const firstPassInput = document.createElement('input');
firstPassInput.type = 'checkbox'; firstPassInput.id = 'first-pass-audition';
let firstPassPreference = true;
let nonClubFirstPassPreference = true;
firstPassLabel.append(firstPassInput, document.createTextNode('Primera pasada · sin revisión global'));
proofLabel.after(firstPassLabel);
const syncProofMode = () => {
  proofInput.disabled = Number($('#duration').value) > 60 || $('#render-mode').value !== 'ai_sovereign';
  if (proofInput.disabled) proofInput.checked = false;
  proofLabel.classList.toggle('disabled', proofInput.disabled);
  firstPassInput.disabled = $('#render-mode').value !== 'ai_sovereign' || proofInput.checked;
  firstPassInput.checked = !firstPassInput.disabled && firstPassPreference;
  firstPassLabel.classList.toggle('disabled', firstPassInput.disabled);
};
$('#duration').addEventListener('change', syncProofMode);
$('#render-mode').addEventListener('change', syncProofMode);
proofInput.addEventListener('change', syncProofMode);
firstPassInput.addEventListener('change', () => { firstPassPreference = firstPassInput.checked; });
const updateClubMode = (club, wasClub) => {
  $('#behavior').disabled = club;
  if (club !== wasClub) {
    if (club) { nonClubFirstPassPreference = firstPassPreference; firstPassPreference = true; }
    else firstPassPreference = nonClubFirstPassPreference;
  }
  syncProofMode();
};
$('#club-mode').addEventListener('change', () => updateClubMode($('#club-mode').checked, !$('#club-mode').checked));
syncProofMode();
// Local development controls remain available without crowding the composition workflow.
const studioOptions = document.createElement('details');
studioOptions.className = 'studio-options';
const studioOptionsTitle = document.createElement('summary');
studioOptionsTitle.textContent = 'Laboratorio de composición';
studioOptions.append(studioOptionsTitle);
const studioOptionsBody = document.createElement('div');
studioOptionsBody.className = 'studio-options-body';
studioOptions.append(studioOptionsBody);
const composeButton = $('#submit');
composeButton.before(studioOptions);
for (const control of [$('#club-layer-pace').closest('label'), $('#club-variety').closest('label'), proofLabel, firstPassLabel, $('#compose-form .advanced')])
  studioOptionsBody.append(control);
studioOptions.before(composeButton);
function studioDialog(id, title) {
  const dialog = document.createElement('dialog');
  dialog.id = id; dialog.className = 'studio-dialog';
  dialog.setAttribute('aria-label', title);
  const header = document.createElement('div'); header.className = 'studio-dialog-header';
  const caption = document.createElement('span'); caption.textContent = title;
  const close = document.createElement('button'); close.type = 'button'; close.className = 'dock-button';
  close.textContent = '×'; close.setAttribute('aria-label', 'Cerrar ' + title);
  close.addEventListener('click', () => dialog.close()); header.append(caption, close); dialog.append(header);
  document.body.append(dialog);
  return dialog;
}
const composeDialog = studioDialog('composition-dialog', 'Nueva composición');
composeDialog.append($('.compose-section'));
const composeError = document.createElement('p'); composeError.className = 'notice'; composeError.hidden = true;
ui.form.append(composeError);
$('.arrangement-head').after(ui.notice);
const sessionDialog = studioDialog('session-dialog', 'Seguimiento de la obra');
sessionDialog.append($('.session-section'));
function openComposeDialog() {
  ui.history.hidden = true;
  if (!composeDialog.open) composeDialog.showModal();
  ui.prompt.focus({ preventScroll: true });
}
$('#sidebar-compose').addEventListener('click', openComposeDialog);
$('#sidebar-library').addEventListener('click', () => $('#history-toggle').click());
$('#sidebar-session').addEventListener('click', () => { if (!sessionDialog.open) sessionDialog.showModal(); });
const draftControls = [...ui.form.querySelectorAll('input,select,textarea')].filter(el => el.id);
function saveCompositionDraft() {
  const draft = Object.fromEntries(draftControls.map(el => [el.id, el.type === 'checkbox' ? el.checked : el.value]));
  try { localStorage.setItem('pulso-composition-draft-v1', JSON.stringify(draft)); } catch {}
}
try {
  const draft = JSON.parse(localStorage.getItem('pulso-composition-draft-v1') || '{}');
  for (const el of draftControls) {
    if (!Object.hasOwn(draft, el.id)) continue;
    if (el.type === 'checkbox' && typeof draft[el.id] === 'boolean') el.checked = draft[el.id];
    else if (typeof draft[el.id] === 'string' && (el.tagName !== 'SELECT' || [...el.options].some(o => o.value === draft[el.id]))) el.value = draft[el.id];
  }
  firstPassPreference = firstPassInput.checked;
  nonClubFirstPassPreference = firstPassPreference;
  updateClubMode($('#club-mode').checked, $('#club-mode').checked);
  $('#prompt-count').textContent = `${ui.prompt.value.length} / 600`;
} catch {}
ui.form.addEventListener('input', saveCompositionDraft);
ui.form.addEventListener('change', saveCompositionDraft);
composeDialog.addEventListener('close', saveCompositionDraft);
const sessionIndicator = document.createElement('button'); sessionIndicator.type = 'button';
sessionIndicator.className = 'session-indicator'; sessionIndicator.textContent = 'Seguimiento de la obra ↗';
sessionIndicator.addEventListener('click', () => sessionDialog.showModal());
const studioStatusbar = document.createElement('div'); studioStatusbar.className = 'studio-statusbar';
ui.health.before(studioStatusbar); studioStatusbar.append(ui.health, sessionIndicator);
new MutationObserver(() => {
  const status = ui.session.querySelector('.session-status')?.textContent?.trim();
  const title = ui.session.querySelector('.session-title')?.textContent?.trim();
  sessionIndicator.textContent = status ? `${status}${title ? ' · ' + title : ''} ↗` : 'Seguimiento de la obra ↗';
}).observe(ui.session, { childList: true, subtree: true, characterData: true });
const states = { queued: 'EN COLA', running: 'COMPONIENDO', completed: 'LISTA', failed: 'FALLIDA', interrupted: 'INTERRUMPIDA', cancelled: 'CANCELADA' };
const palette = { pad: '#dfb64e', bass: '#71a1d0', keys: '#75b57c', pluck: '#e37957', lead: '#e37957', drums: '#75b57c', synth: '#cfd0c7' };
const names = { pad: 'Seno · armonía', bass: 'Seno · bajo', keys: 'Seno · teclas', pluck: 'Seno · arpegio', lead: 'Seno · melodía', drums: 'Seno · percusión', synth: 'Seno · pista' };
const fmt = seconds => { const value = Math.max(0, Math.floor(seconds || 0)); return `${String(Math.floor(value / 60)).padStart(2, '0')}:${String(value % 60).padStart(2, '0')}`; };
const node = (tag, className, value) => { const el = document.createElement(tag); if (className) el.className = className; if (value !== undefined) el.textContent = value; return el; };
const fileUrl = (job, filename) => `/api/jobs/${job.id}/files/${encodeURIComponent(filename)}`;
const download = (job, filename, label, className = '') => { const a = node('a', className, label); a.href = fileUrl(job, filename); a.download = filename; return a; };
const number = value => new Intl.NumberFormat('es-AR').format(Number(value) || 0);
const linkedJobId = new URLSearchParams(location.search).get('job');
let jobs = [], selectedId = /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(linkedJobId || '') ? linkedJobId : null, loadedKey = null, loadVersion = 0, current = null, barPx = 22, playhead = null, previewDiagnostic = false, lastAutoRecoverySelection = null, historyCategory = 'personal';
const evaluationsByJob = new Map();
let selectedBenchmarkCase = null;
const audio = new ProductionAudio(updatePosition, () => { ui.play.textContent = '▶'; ui.play.setAttribute('aria-label', 'Reproducir'); });
ui.evaluate = node('button', 'ghost-button', 'Evaluar');
ui.evaluate.type = 'button'; ui.evaluate.id = 'evaluate'; ui.evaluate.disabled = true;
ui.evaluate.title = 'Puntuar esta obra y guardar tu escucha para futuras mejoras';
$('#history-toggle').before(ui.evaluate);
ui.evaluate.addEventListener('click', openEvaluationDialog);
ui.lab = node('button', 'ghost-button', 'Laboratorio');
ui.lab.type = 'button'; ui.lab.id = 'lab';
ui.lab.title = 'Banco de pruebas, comparaciones A/B e informe musical';
$('#history-toggle').before(ui.lab);
ui.lab.addEventListener('click', () => openEvaluationLab());
ui.share = node('button', 'ghost-button', 'Share');
ui.share.type = 'button';
ui.share.disabled = true;
ui.share.title = 'Compartir esta obra en un visualizador de solo lectura';
ui.share.setAttribute('aria-label', 'Compartir obra');
$('#history-toggle').before(ui.share);
ui.share.addEventListener('click', openShareDialog);
const patchStorageKey = (jobId, filename) => `pulso:audition:${SOUND_BANK_VERSION}:${jobId}:${filename}`;
function updateListeningLabels() {
  $('#listen-description').textContent = audio.mode === 'piano'
    ? 'Piano MIDI: todas las pistas suenan con el mismo piano para escuchar la composición con claridad. El MIDI no cambia.'
    : 'MIDI neutro: monitor senoidal sin color instrumental. El MIDI no cambia.';
  if (!current) return;
  ui.timeline.querySelectorAll('.track-row').forEach((row, index) => {
    const track = current.tracks[index];
    const label = row.querySelector('.sound-label');
    const select = row.querySelector('.sound-select');
    if (label) label.textContent = audio.mode === 'piano' ? 'Piano MIDI' : audio.mode === 'production' ? PATCHES[track.patchId]?.label || 'Sonido PULSO' : names[track.instrument] || names.synth;
    if (select) { select.disabled = audio.mode !== 'production'; select.hidden = audio.mode !== 'production'; }
  });
}

async function api(url, options = {}) { const response = await fetch(url, { cache: 'no-store', ...options }); const result = await response.json(); if (!response.ok) throw new Error(result.error || `Error HTTP ${response.status}`); return result; }
function showNotice(message) { ui.notice.textContent = message || ''; ui.notice.hidden = !message; }
function setExport(job, filename) {
  ui.evaluate.disabled = exhibitionMode || !job?.manifest || job.state !== 'completed' ||
    !current || current.job.id !== job.id || filename !== job.manifest.fullFile;
  ui.share.disabled = exhibitionMode || !job?.manifest || job.state !== 'completed' ||
    !current || current.job.id !== job.id || filename !== job.manifest.fullFile;
  ui.exportOne.disabled = !job?.manifest?.fullFile || filename !== job.manifest.fullFile || !current || current.job.id !== job.id;
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
  selectedBenchmarkCase = null;
  ui.prompt.value = job.request.prompt;
  $('#allow-chromatic').checked = job.request.allow_chromatic === true;
  $('#club-layer-pace').value = job.request.club_layer_pace || 'auto';
  $('#club-variety').checked = job.request.club_variety === true;
  $('#duration').value = String(job.request.duration_seconds);
  $('#render-mode').value = job.request.ai_sovereign ? 'ai_sovereign' : 'standard';
  proofInput.checked = Boolean(job.request.proof_mode);
  firstPassPreference = Boolean(job.request.first_pass_audition);
  syncProofMode();
  $('#bpm').value = String(job.request.bpm);
  $('#club-mode').checked = job.request.behavior === 'club';
  $('#behavior').disabled = $('#club-mode').checked;
  $('#behavior').value = job.request.behavior === 'club' ? 'adaptive' : job.request.behavior || 'adaptive';
  $('#seed').value = job.request.seed;
  $('#prompt-count').textContent = `${ui.prompt.value.length} / 600`;
  ui.history.hidden = true;
  showNotice('Ajustes cargados. Revisa antes de componer: todavía no se envió ninguna solicitud.');
  ui.prompt.focus();
}
async function resumeAcceptedMidi(job, button) {
  if (!confirm('¿Retomar desde el último MIDI aceptado? Se hará una nueva solicitud a la IA, pero no se volverán a escribir las ventanas ya aprobadas.')) return;
  button.disabled = true;
  showNotice('Preparando la reanudación desde el MIDI aceptado...');
  const request = job.request;
  const payload = {
    prompt: request.prompt,
    duration_seconds: request.duration_seconds,
    bpm: request.bpm,
    behavior: request.behavior,
    club_layer_pace: request.club_layer_pace || 'auto',
    club_variety: request.club_variety === true,
    render_mode: 'ai_sovereign',
    seed: request.seed,
    proof_mode: false,
    first_pass_audition: request.first_pass_audition === true,
    ...(typeof request.allow_chromatic === 'boolean' ? { allow_chromatic: request.allow_chromatic } : {}),
    ...(request.benchmark_case_id ? { benchmark_case_id: request.benchmark_case_id } : {}),
    phrase_architecture_experiment: request.phrase_architecture_experiment === true,
    resume_from_job: job.id,
  };
  try {
    const route = job.origin === 'auto' ? '/api/auto/jobs' : '/api/jobs';
    const result = await api(route, { method: 'POST', headers: {
      'Content-Type': 'application/json', 'X-Pulso-Local': '1',
    }, body: JSON.stringify(payload) });
    selectedId = result.job.id;
    loadedKey = null;
    previewDiagnostic = false;
    showNotice('Reanudación iniciada. El material aceptado se conserva.');
    await refresh();
  } catch (error) {
    showNotice(error.message);
  } finally {
    button.disabled = false;
  }
}
async function reviewFirstPass(job, button) {
  if (!confirm('¿Crear una revisión IA de esta primera pasada? Conserva el plan y el MIDI como punto de partida y realiza nuevas llamadas de pago a la API. En Club revisa principalmente armonía, capas y cierre; no rediseña la idea rítmica. La obra original no cambia.')) return;
  button.disabled = true;
  const request = job.request;
  try {
    const route = job.origin === 'auto' ? '/api/auto/jobs' : '/api/jobs';
    const result = await api(route, { method: 'POST', headers: {
      'Content-Type': 'application/json', 'X-Pulso-Local': '1',
    }, body: JSON.stringify({
      prompt: request.prompt, duration_seconds: request.duration_seconds, bpm: request.bpm,
      behavior: request.behavior, render_mode: 'ai_sovereign', seed: request.seed,
      allow_chromatic: request.allow_chromatic === true, proof_mode: false,
      first_pass_audition: false,
      phrase_architecture_experiment: request.phrase_architecture_experiment === true,
      ...(request.benchmark_case_id ? { benchmark_case_id: request.benchmark_case_id } : {}),
      review_from_job: job.id,
    }) });
    selectedId = result.job.id; loadedKey = null; previewDiagnostic = false;
    showNotice('Revisión IA iniciada. La primera pasada permanece intacta para comparar.');
    await refresh();
  } catch (error) { showNotice(error.message); }
  finally { button.disabled = false; }
}
function renderSession(job) {
  if (!job) return;
  const articulation = job.manifest?.articulationReview;
  const articulationRevision = !articulation && jobs.find(candidate =>
    candidate.manifest?.articulationReview?.sourceJobId === job.id);
  ui.session.replaceChildren();
  ui.session.append(node('h3', 'session-title', job.manifest?.title || 'Composición en curso'));
  ui.session.append(node('div', `session-status ${job.state}`,
    job.state === 'completed' && articulation ? 'REVISIÓN DE ARTICULACIÓN' :
    job.state === 'completed' && job.manifest?.first_pass_audition
      ? 'PRIMERA PASADA' : job.state === 'completed' && job.manifest?.review_audition
        ? 'REVISIÓN A/B' : states[job.state] || job.state.toUpperCase()));
  ui.session.append(node('p', 'session-prompt', job.request.prompt));
  const savedEvaluation = evaluationsByJob.get(job.id);
  if (savedEvaluation) ui.session.append(node('p', 'session-note',
    `Tu evaluación: ${savedEvaluation.would_use === 'yes' ? 'la usaría' :
      savedEvaluation.would_use === 'maybe' ? 'tal vez la usaría' : 'no la usaría'} · ` +
    `actualizada ${new Date(savedEvaluation.updated_at).toLocaleString('es-AR')}.`));
  if (typeof job.request.allow_chromatic === 'boolean')
    ui.session.append(node('p', 'session-note', job.request.allow_chromatic
      ? 'Cromatismos permitidos.' : 'Sin cromatismos: escala declarada estricta.'));
  const date = new Date(job.request.created_at).toLocaleString('es-AR', { dateStyle: 'short', timeStyle: 'short' });
  ui.session.append(node('p', 'session-meta', `${date} · ${fmt(job.request.duration_seconds)} · ${job.request.bpm} BPM · semilla ${job.request.seed}`));
  if (articulation || articulationRevision) {
    const pair = node('p', 'session-note');
    const sourceId = articulation?.sourceJobId || job.id;
    const revisionId = articulation ? job.id : articulationRevision.id;
    const original = node('a', '', 'ORIGINAL');
    original.href = `?job=${encodeURIComponent(sourceId)}`;
    const revised = node('a', '', 'REVISIÓN');
    revised.href = `?job=${encodeURIComponent(revisionId)}`;
    pair.append(original, document.createTextNode('  >  '), revised);
    ui.session.append(pair);
    if (articulation) ui.session.append(node('p', 'session-note',
      `Articulación revisada por IA: ${articulation.changedNotes} duraciones de ${job.manifest.all_midi_notes} notas. Alturas, entradas, velocidades y otras pistas intactas.`));
  }
  if (job.request.editorial_profile === 'ai-only-v2') ui.session.append(node('p', 'session-note', 'Perfil editorial local IA v2 · cada nota es autoría IA.'));
  if (job.request.behavior === 'club') ui.session.append(node('p', 'session-note', 'Club · células reconocibles y evolución de capas. Horizonte aproximado de 64 compases, sin cambios obligatorios.'));
  if (job.request.first_pass_audition && !articulation) ui.session.append(node('p', 'session-note', 'Primera pasada IA: sin revisión global. Solo se revisan huecos armónicos medidos si contradicen el plan.'));
  if (job.request.review_from_job && !articulation) {
    const pair = node('p', 'session-note');
    const original = node('a', '', 'ORIGINAL');
    original.href = `?job=${encodeURIComponent(job.request.review_from_job)}`;
    const revised = node('a', '', 'REVISIÓN');
    revised.href = `?job=${encodeURIComponent(job.id)}`;
    pair.append(original, document.createTextNode('  >  '), revised);
    ui.session.append(pair);
    ui.session.append(node('p', 'session-note',
      `Revisión A/B de la primera pasada ${job.request.review_from_job.slice(0, 8)}: se conserva el mismo plan y solo la revisión IA puede cambiar notas.`));
  }
  if (job.request.phrase_architecture_experiment) ui.session.append(node('p', 'session-note',
    'Plan de frases y relaciones entre voces escrito por la IA antes del MIDI. Las notas siguen siendo íntegramente autoría de la IA.'));
  if (job.phraseAudit?.kind === 'club_production_v1') ui.session.append(node('p', 'session-note',
    `Producción Club: ${job.phraseAudit.layers.length} capas; ${job.phraseAudit.issues.length} diferencias entre el plan y el MIDI. La escucha musical sigue siendo necesaria.`));
  else if (job.phraseAudit) ui.session.append(node('p', 'session-note',
    `Plan de frases: ${job.phraseAudit.phrases.length} intenciones; ${job.phraseAudit.silentSpeakerPhrases} sin notas del portavoz y ${job.phraseAudit.plannedResponsesWithoutNotes} respuestas sin notas. Estos datos no califican la musicalidad: escuchá el diálogo real.`));
  if (job.state === 'running' || job.state === 'queued') {
    const elapsed = Math.max(0, (Date.now() - Date.parse(job.request.created_at)) / 1000);
    ui.session.append(node('p', 'session-note', `${job.stage || 'Iniciando'} · ${fmt(elapsed)}${job.total ? ` · ${job.completed}/${job.total}` : ''}`));
    const progress = node('div', 'progress-track'), fill = node('i'); fill.style.width = job.total ? `${Math.min(100, 100 * job.completed / job.total)}%` : '12%'; progress.append(fill); ui.session.append(progress);
  }
  if (job.error) ui.session.append(node('p', 'notice', job.error));
  if (job.checkpoint?.file && !job.manifest) ui.session.append(node('p', 'session-note', `Hay MIDI provisional: ${job.checkpoint.parts || 0} pistas con notas y ${job.checkpoint.notes || 0} notas. No es una obra terminada ni auditada.`));
  if (job.request.automatic_recovery_attempt) ui.session.append(node('p', 'session-note', 'Recuperación automática desde el último MIDI aceptado. Conserva las pistas previas; límite de 10 minutos y USD 0,75 estimados para este intento. Una llamada ya iniciada puede superar ese importe.'));
  if (job.recovery?.jobId) ui.session.append(node('p', 'session-note', 'PULSO inició una recuperación automática desde esta obra. El material aceptado permanece guardado.'));
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
  if (job.compositionAudit) {
    const audit = job.compositionAudit;
    const box = node('div', 'editorial');
    box.append(node('strong', '', 'Desarrollo y procedencia MIDI'));
    box.append(node('span', '', `${number(audit.exactRepeatedEightBarNotes)} de ${number(audit.totalNotes)} notas en bloques de 8 compases idénticos (${Math.round(audit.exactRepeatedEightBarRatio * 100)}%). ${number(audit.crossTrackExactNoteInstances)} notas coinciden exactamente entre pistas.`));
    const source = audit.noteProvenance;
    if (source) box.append(node('span', '', `Frases IA: ${number(source.direct_ai)} notas de primer uso · ${number(source.reused_ai)} reutilizadas · ${number(source.transformed_ai)} transformadas · ${number(source.reused_transformed_ai)} reutilizadas y transformadas.`));
    else box.append(node('span', '', 'La procedencia de cada nota no está disponible para esta obra anterior; la repetición exacta sí se midió desde el MIDI.'));
    const repeated = [...audit.tracks].sort((a, b) => b.exactRepeatedEightBarNotes - a.exactRepeatedEightBarNotes).slice(0, 3);
    box.append(node('span', '', `Más repetición literal: ${repeated.map(track => `${track.name} (${track.exactRepeatedEightBarNotes})`).join(' · ')}.`));
    ui.session.append(box);
  }
  const actions = node('div', 'session-actions');
  actions.append(actionButton('Repetir ajustes', () => reuse(job)));
  if (job.state === 'completed' && job.manifest?.first_pass_audition &&
      job.request.ai_sovereign && job.manifest.fullFile) {
    const reviewButton = actionButton(job.request.behavior === 'club'
      ? 'Revisar armonía y cierre con IA' : 'Revisar esta primera pasada con IA',
      () => reviewFirstPass(job, reviewButton));
    actions.append(reviewButton);
  }
  if (job.checkpoint?.file && !job.manifest &&
      (job.state === 'failed' || job.state === 'interrupted')) {
    const resumeButton = actionButton('Retomar MIDI aceptado', () => resumeAcceptedMidi(job, resumeButton));
    actions.append(resumeButton);
  }
  if (job.recovery?.jobId) actions.append(actionButton('Ver recuperación', () => {
    selectedId = job.recovery.jobId; loadedKey = null; previewDiagnostic = false; showSelected();
  }));
  if (job.manifest?.fullFile) actions.append(actionButton('↓ Todas las notas · 1 pista MIDI', async () => {
    try { await downloadSingleTrackMidi(fileUrl(job, job.manifest.fullFile), `pulso-${job.id.slice(0, 8)}-todas-las-notas.mid`); }
    catch (error) { showNotice(`No se pudo crear el MIDI de una pista: ${error.message}`); }
  }, 'primary-link'));
  if (job.manifest?.fullFile) actions.append(download(job, job.manifest.fullFile, '↓ Obra original · multipista'));
  if (job.checkpoint?.file && !job.manifest) actions.append(download(job, job.checkpoint.file, '↓ MIDI provisional'));
  if (job.diagnostic?.file && !job.manifest) actions.append(download(job, job.diagnostic.file, '↓ MIDI rechazado (diagnóstico)'));
  if (job.diagnostic?.planFile && !job.manifest) actions.append(download(job, job.diagnostic.planFile, 'Plan del candidato'));
  if (job.diagnostic?.auditFile && !job.manifest) actions.append(download(job, job.diagnostic.auditFile, 'Auditoría del candidato'));
  if (job.diagnostic?.file && !job.manifest) actions.append(actionButton(previewDiagnostic ? 'Escuchar último aceptado' : 'Escuchar candidato rechazado', () => {
    previewDiagnostic = !previewDiagnostic; loadedKey = null; showSelected();
  }));
  if (job.manifest?.comparisonFile) actions.append(download(job, job.manifest.comparisonFile, 'Referencia A/B'));
  if (job.manifest?.planFile) actions.append(download(job, job.manifest.planFile, 'Plan compositivo'));
  if (job.manifest?.phraseArchitectureFile) actions.append(download(job, job.manifest.phraseArchitectureFile,
    job.manifest.clubProductionPlanFile ? 'Plan de producción Club' : 'Plan de frases IA'));
  if (job.phraseAudit) actions.append(download(job, 'phrase-audit.json', 'Contraste plan/MIDI'));
  if (job.manifest?.auditFile) actions.append(download(job, job.manifest.auditFile, 'Auditoría musical'));
  if (job.compositionAudit) actions.append(download(job, 'composition-audit.json', 'Desarrollo MIDI'));
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
  const personal = jobs.filter(job => job.origin !== 'auto');
  const auto = jobs.filter(job => job.origin === 'auto');
  $('#personal-count').textContent = String(personal.length);
  $('#auto-count').textContent = String(auto.length);
  for (const category of ['personal', 'auto']) {
    const tab = $(`#history-${category}`);
    const active = historyCategory === category;
    tab.classList.toggle('active', active);
    tab.setAttribute('aria-selected', String(active));
  }
  const visible = historyCategory === 'auto' ? auto : personal;
  if (!visible.length) { ui.jobs.append(node('p', 'session-note', historyCategory === 'auto' ? 'Todavía no hay pruebas automáticas.' : 'Todavía no hay obras personales.')); return; }
  for (const job of visible) {
    const button = node('button', `history-job ${selectedId === job.id ? 'selected' : ''}`);
    button.type = 'button';
    button.append(node('strong', '', job.manifest?.title || job.request.prompt.slice(0, 68)));
    button.append(node('span', 'status ' + job.state, job.manifest?.articulationReview
      ? 'REVISIÓN DE ARTICULACIÓN' : job.manifest?.review_audition
      ? 'REVISIÓN A/B' : job.manifest?.first_pass_audition
        ? 'PRIMERA PASADA' : states[job.state] || job.state));
    button.append(node('span', '', `${new Date(job.request.created_at).toLocaleString('es-AR', { dateStyle: 'short', timeStyle: 'short' })} · ${fmt(job.request.duration_seconds)} · ${job.manifest?.tracks?.length || job.checkpoint?.parts || 0} pistas${job.origin === 'auto' ? ` · prueba ${job.id.slice(0, 8)}` : ''}`));
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
  ui.share.disabled = true;
  ui.play.disabled = true; ui.exportOne.disabled = true; $('#preview-wav').disabled = true; $('#full-wav').disabled = true;
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
        if (notes.length) {
          const track = { name: part.track.name, notes, filename: part.track.filename, instrument: instrumentFor(part.track.instrument || {}, part.track.name), meta: part.track.instrument || {} };
          track.patchId = patchForTrack(track);
          try { const saved = localStorage.getItem(patchStorageKey(job.id, track.filename)); if (PATCHES[saved]?.family === PATCHES[track.patchId]?.family) track.patchId = saved; } catch { /* Private browsing may disable storage. */ }
          tracks.push(track);
        }
      }
    }
    if (!tracks.length) throw new Error('El MIDI no contiene notas reproducibles.');
    current = { job, midi: timing, tracks };
    audio.setSong(timing, tracks);
    renderTimeline();
    updateListeningLabels();
    $('#song-title').textContent = diagnostic ? 'MIDI rechazado - diagnóstico' : job.manifest?.title || 'MIDI provisional';
    $('#song-subtitle').classList.add('score-facts');
    $('#song-subtitle').replaceChildren(
      node('strong', '', job.manifest?.key || 'MIDI provisional'),
      node('span', '', `${job.manifest?.bars || Math.ceil(timing.lengthBeats / timing.beatsPerBar)} compases`),
      node('span', '', fmt(audio.duration)),
      node('span', '', `${Math.round(job.manifest?.bpm || 60e6 / timing.tempos[0].microseconds)} BPM`),
      node('span', '', `${tracks.length} pistas`));
    $('#song-bpm').textContent = `${Math.round(job.manifest?.bpm || 60e6 / timing.tempos[0].microseconds)} BPM`;
    $('#song-key').textContent = job.manifest?.key || 'MIDI provisional';
    $('#song-stat').textContent = '';
    $('#track-summary').textContent = `${tracks.length} pistas · ${fmt(audio.duration)}`;
    ui.play.disabled = false;
    $('#preview-wav').disabled = false;
    $('#full-wav').disabled = !job.manifest || diagnostic;
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
    const soundLabel = node('small', 'sound-label', audio.mode === 'piano' ? 'Piano MIDI' : audio.mode === 'production' ? PATCHES[track.patchId]?.label || 'Sonido PULSO' : names[track.instrument] || names.synth);
    const patchSelect = node('select', 'sound-select');
    patchSelect.setAttribute('aria-label', `Sonido de ${track.name}`);
    patchSelect.title = `Cambiar el sonido de ${track.name} sin alterar su MIDI`;
    for (const id of patchOptions(PATCHES[track.patchId]?.family || 'synth')) patchSelect.add(new Option(PATCHES[id].label, id));
    patchSelect.value = track.patchId;
    patchSelect.disabled = audio.mode !== 'production';
    patchSelect.hidden = audio.mode !== 'production';
    patchSelect.addEventListener('change', () => {
      if (!audio.setPatch(index, patchSelect.value)) return;
      soundLabel.textContent = PATCHES[patchSelect.value].label;
      try { localStorage.setItem(patchStorageKey(job.id, track.filename), patchSelect.value); } catch { /* Session still works. */ }
    });
    bottom.append(mute, solo, soundLabel, patchSelect);
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
  if (exhibitionMode) return loadExhibition();
  try {
    const [status, listing] = await Promise.all([api('/api/status'), api('/api/jobs')]);
    jobs = listing.jobs;
    const ready = status.workerReady && status.apiKeyConfigured;
    ui.health.className = `health ${ready ? '' : 'warn'}`;
    const profileLabel = status.musicalProfile === 'connected-discourse'
      ? 'Laboratorio local de fraseo y diálogo conectado'
      : `Perfil editorial local ${status.editorialProfile === 'ai-only-v2' ? 'IA v2' : 'anterior'}`;
    ui.health.textContent = ready ? '0.1.0 version Beta' : `0.1.0 version Beta · Composición no disponible: ${status.workerReady ? '' : 'falta el generador. '}${status.apiKeyConfigured ? '' : 'falta OPENAI_API_KEY en este proceso.'} Las obras guardadas siguen disponibles.`;
    ui.submit.disabled = Boolean(status.activeJobId) || !ready;
    $('#job-count').textContent = `${jobs.length} OBRAS`;
    if (!jobs.length) {
      if (selectedId || current) {
        audio.stop(); current = null; playhead = null; loadedKey = null; ++loadVersion;
        ui.play.disabled = true; setExport(null, null);
        $('#preview-wav').disabled = true;
        $('#full-wav').disabled = true;
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
    const selected = jobs.find(job => job.id === selectedId);
    if (selected?.recovery?.jobId && selected.id !== linkedJobId &&
        lastAutoRecoverySelection !== selected.id &&
        jobs.some(job => job.id === selected.recovery.jobId)) {
      lastAutoRecoverySelection = selected.id;
      selectedId = selected.recovery.jobId;
      loadedKey = null;
      previewDiagnostic = false;
    }
    renderHistory(); showSelected();
  } catch (error) { ui.health.className = 'health warn'; ui.health.textContent = `No se pudo conectar con el estudio local: ${error.message}`; ui.submit.disabled = true; }
}
ui.form.addEventListener('submit', async event => {
  event.preventDefault(); showNotice(''); ui.submit.disabled = true;
  const payload = { club_layer_pace: $('#club-layer-pace').value, club_variety: $('#club-variety').checked, prompt: ui.prompt.value, allow_chromatic: $('#allow-chromatic').checked, duration_seconds: Number($('#duration').value), bpm: Number($('#bpm').value), behavior: $('#club-mode').checked ? 'club' : $('#behavior').value, render_mode: $('#render-mode').value, seed: $('#seed').value.trim(), proof_mode: proofInput.checked && !proofInput.disabled, first_pass_audition: firstPassInput.checked && !firstPassInput.disabled };
  if (selectedBenchmarkCase && selectedBenchmarkCase.prompt === payload.prompt.trim() &&
      selectedBenchmarkCase.duration_seconds === payload.duration_seconds &&
      selectedBenchmarkCase.bpm === payload.bpm &&
      selectedBenchmarkCase.behavior === payload.behavior)
    payload.benchmark_case_id = selectedBenchmarkCase.id;
  composeError.hidden = true;
  try { const result = await api('/api/jobs', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Pulso-Local': '1' }, body: JSON.stringify(payload) }); selectedId = result.job.id; loadedKey = null; previewDiagnostic = false; saveCompositionDraft(); composeDialog.close(); await refresh(); }
  catch (error) { showNotice(error.message); composeError.textContent = error.message; composeError.hidden = false; ui.submit.disabled = false; }
});
ui.prompt.addEventListener('input', () => {
  $('#prompt-count').textContent = `${ui.prompt.value.length} / 600`;
  if (selectedBenchmarkCase?.prompt !== ui.prompt.value.trim()) selectedBenchmarkCase = null;
});
$('#history-toggle').addEventListener('click', () => { ui.history.hidden = !ui.history.hidden; });
for (const category of ['personal', 'auto']) $('#history-' + category).addEventListener('click', () => {
  historyCategory = category;
  renderHistory();
});
$('#history-close').addEventListener('click', () => { ui.history.hidden = true; });
$('#refresh').addEventListener('click', refresh);
ui.play.addEventListener('click', async () => { try { if (audio.playing) audio.pause(); else await audio.play(); ui.play.textContent = audio.playing ? 'Ⅱ' : '▶'; ui.play.setAttribute('aria-label', audio.playing ? 'Pausar' : 'Reproducir'); } catch (error) { showNotice(`No se pudo iniciar el audio: ${error.message}`); } });
ui.stop.addEventListener('click', () => audio.stop());
ui.back.addEventListener('click', () => audio.seek(0));
ui.exportOne.addEventListener('click', async () => {
  if (!current?.job.manifest?.fullFile) return;
  const job = current.job;
  ui.exportOne.disabled = true;
  try { await downloadSingleTrackMidi(fileUrl(job, job.manifest.fullFile), `pulso-${job.id.slice(0, 8)}-todas-las-notas.mid`); }
  catch (error) { showNotice(`No se pudo crear el MIDI de una pista: ${error.message}`); }
  finally { ui.exportOne.disabled = !current || current.job.id !== job.id; }
});
$('#volume').addEventListener('input', event => audio.setVolume(Number(event.target.value) / 100));
$('#listen-mode').addEventListener('change', async event => {
  const selected = event.target.value;
  try { await audio.setMode(selected); updateListeningLabels(); }
  catch (error) { event.target.value = audio.mode; showNotice(error.message || 'No se pudo cargar el piano.'); }
});
$('#preview-wav').addEventListener('click', async () => {
  if (!current) return;
  const button = $('#preview-wav');
  button.disabled = true; button.textContent = 'Renderizando…';
  try {
    const start = audio.currentPosition();
    const blob = await audio.renderPreview(start, 30);
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `pulso-${current.job.id.slice(0, 8)}-${Math.floor(start)}s-preview.wav`;
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
    showNotice(`Vista previa WAV lista: ${fmt(start)} a ${fmt(Math.min(audio.duration, start + 30))}. No se cambió el MIDI.`);
  } catch (error) { showNotice(`No se pudo crear el WAV: ${error.message}`); }
  finally { button.textContent = 'WAV 30 s'; button.disabled = !current; }
});
$('#full-wav').addEventListener('click', async () => {
  if (!current?.job.manifest) return;
  const jobId = current.job.id;
  const button = $('#full-wav');
  button.disabled = true; button.textContent = 'Exportando WAV 0%';
  try {
    const blob = await audio.renderFullWav(progress => { button.textContent = `Exportando WAV ${Math.round(progress * 100)}%`; });
    const url = URL.createObjectURL(blob);
    const link = document.createElement('a');
    link.href = url;
    link.download = `pulso-${jobId.slice(0, 8)}-obra-completa.wav`;
    link.click();
    setTimeout(() => URL.revokeObjectURL(url), 60000);
    showNotice('Obra completa WAV lista. Se exportó la mezcla audible, sin modificar el MIDI ni consumir API.');
  } catch (error) { showNotice(`No se pudo exportar el audio completo: ${error.message}`); }
  finally { button.textContent = 'WAV completo'; button.disabled = !current?.job.manifest; }
});
document.addEventListener('visibilitychange', () => { if (document.hidden && audio.playing) { audio.pause(); ui.play.textContent = '▶'; } });
if (exhibitionMode) configureExhibition();
refresh().then(() => {
  if (!exhibitionMode && /^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(comparisonId || ''))
    openEvaluationLab(comparisonId);
});
if (!exhibitionMode) setInterval(refresh, 2000);

function openShareDialog() {
  if (ui.share.disabled || !current?.job.manifest || current.job.state !== 'completed') return;
  const job = current.job;
  const url = new URL('/', location.origin);
  url.searchParams.set('share', job.id);
  const dialog = node('dialog', 'share-dialog');
  const heading = node('h2', '', 'Comparte tu obra');
  heading.id = 'share-heading';
  dialog.setAttribute('aria-labelledby', heading.id);
  const close = actionButton('Cerrar', () => dialog.close(), 'ghost-button');
  const header = node('div', 'share-dialog-head'); header.append(heading, close);
  const title = node('p', 'share-work-title', job.manifest.title || 'Obra PULSO');
  const description = node('p', '', 'Una exhibición de solo lectura: la partitura y su reproductor, sin el estudio ni tus registros.');
  const label = node('label', '', 'Enlace de la exhibición');
  label.htmlFor = 'share-url';
  const input = node('input'); input.id = 'share-url'; input.type = 'url';
  input.readOnly = true; input.value = url.href;
  input.addEventListener('focus', () => input.select());
  const feedback = node('p', 'share-feedback'); feedback.setAttribute('role', 'status');
  const copy = actionButton('Copiar enlace', async () => {
    try {
      if (!navigator.clipboard?.writeText) throw new Error('Clipboard unavailable');
      await navigator.clipboard.writeText(url.href);
      feedback.textContent = 'Enlace copiado.';
    } catch {
      input.focus(); input.select();
      feedback.textContent = 'Copia el enlace seleccionado con Ctrl+C o ⌘C.';
    }
  }, 'dark-button');
  const open = node('a', 'ghost-button', 'Abrir exhibición');
  open.href = url.href; open.target = '_blank'; open.rel = 'noopener noreferrer';
  const actions = node('div', 'share-actions'); actions.append(copy, open);
  const note = node('p', 'share-local-note', 'Enlace local: abre en este equipo mientras el estudio esté encendido. No publica la obra en Internet ni concede acceso público a Cloud.');
  dialog.append(header, title, description, label, input, actions, feedback, note);
  dialog.addEventListener('close', () => { dialog.remove(); ui.share.focus(); }, { once: true });
  document.body.append(dialog);
  dialog.showModal(); input.focus(); input.select();
}

function dialogShell(title) {
  const dialog = node('dialog', 'evaluation-dialog');
  const heading = node('h2', '', title);
  const close = actionButton('Cerrar', () => dialog.close(), 'ghost-button');
  const header = node('div', 'share-dialog-head'); header.append(heading, close);
  const body = node('div', 'evaluation-body');
  const feedback = node('p', 'share-feedback'); feedback.setAttribute('role', 'status');
  dialog.append(header, body, feedback);
  dialog.addEventListener('close', () => dialog.remove(), { once: true });
  document.body.append(dialog);
  dialog.showModal();
  return { dialog, body, feedback };
}

function labeledSelect(title, options, selected) {
  const wrap = node('div', 'evaluation-field');
  const label = node('label', '', title);
  const select = node('select');
  for (const [value, caption] of options) select.add(new Option(caption, value));
  select.value = selected || options[0][0];
  wrap.append(label, select);
  return { wrap, select };
}

function labeledText(title, value = '', multiline = false) {
  const wrap = node('div', 'evaluation-field');
  const label = node('label', '', title);
  const input = node(multiline ? 'textarea' : 'input');
  input.value = value; input.maxLength = multiline ? 1200 : 240;
  if (multiline) input.rows = 3;
  wrap.append(label, input);
  return { wrap, input };
}

async function openEvaluationDialog() {
  if (ui.evaluate.disabled || !current?.job.manifest) return;
  const job = current.job;
  const shell = dialogShell('Evaluar la obra');
  shell.body.append(node('p', 'evaluation-intro',
    `${job.manifest.title} · Tu escucha queda vinculada a este MIDI. Podés actualizar la ficha después.`));
  try {
    const { evaluation, reference, criteria, problemTags } = await api(`/api/evaluations/${job.id}`);
    if (!shell.dialog.open) return;
    const referenceBox = node('div', 'evaluation-reference');
    if (reference) {
      referenceBox.append(node('strong', '', `Referencia anterior: ${reference.title}`));
      referenceBox.append(node('p', '', 'Mismo prompt, semilla, duración, tempo y enfoque. Estos puntajes son solo una referencia; no se copian a esta ficha.'));
      const previous = node('a', '', 'Escuchar la versión anterior');
      previous.href = `/?job=${encodeURIComponent(reference.job_id)}`;
      previous.target = '_blank'; previous.rel = 'noopener noreferrer';
      referenceBox.append(previous);
    } else {
      referenceBox.append(node('p', '', 'No hay una evaluación anterior comparable para este prompt y estos ajustes.'));
    }
    shell.body.append(referenceBox);
    const form = node('form', 'evaluation-form');
    const scores = node('div', 'evaluation-scores');
    for (const [key, title] of Object.entries(criteria)) {
      const field = node('fieldset', 'score-row');
      field.append(node('legend', '', title));
      const previousScore = reference?.evaluation?.scores?.[key];
      if (Number.isInteger(previousScore))
        field.append(node('span', 'previous-score', `Anterior ${previousScore}/5`));
      for (let value = 1; value <= 5; value++) {
        const label = node('label', 'score-choice');
        const radio = node('input'); radio.type = 'radio'; radio.name = `score-${key}`;
        radio.value = String(value); radio.required = true;
        radio.checked = evaluation?.scores?.[key] === value;
        label.append(radio, document.createTextNode(String(value)));
        field.append(label);
      }
      scores.append(field);
    }
    form.append(node('p', 'evaluation-help', '1 = no funciona · 3 = prometedor pero incompleto · 5 = convincente. Puntúa lo que escuchaste, no lo que promete el plan.'));
    form.append(scores);
    const scope = labeledSelect('¿Cuánto escuchaste?', [['full', 'La obra completa'], ['excerpt', 'Solo fragmentos']], evaluation?.scope);
    const listening = labeledSelect('¿Cómo la escuchaste?', [['piano', 'Piano MIDI'], ['neutral', 'Senoidal neutro'], ['live', 'En Ableton Live'], ['other', 'Otro sonido']], evaluation?.listening_mode || audio.mode);
    const use = labeledSelect('¿La usarías en una producción?', [['no', 'No, necesita otra composición'], ['maybe', 'Tal vez, con trabajo musical'], ['yes', 'Sí, es material utilizable']], evaluation?.would_use);
    const confidence = labeledSelect('Seguridad de tu juicio', [['high', 'Alta'], ['medium', 'Media'], ['low', 'Baja']], evaluation?.confidence);
    const fields = node('div', 'evaluation-grid');
    fields.append(scope.wrap, listening.wrap, use.wrap, confidence.wrap);
    form.append(fields);
    form.append(node('h3', '', 'Problemas principales (hasta seis)'));
    const tagLabels = { sin_idea: 'Sin idea central', repeticion_vacia: 'Repetición vacía',
      voces_desconectadas: 'Voces desconectadas', disonancia: 'Choques armónicos',
      ritmo_torpe: 'Ritmo torpe', demasiadas_notas: 'Demasiadas notas',
      muy_vacio: 'Demasiado vacío', climax_plano: 'Clímax plano',
      final_abrupto: 'Final abrupto', timbre_confunde: 'El timbre confunde', otro: 'Otro' };
    const tags = node('div', 'evaluation-tags');
    for (const tag of problemTags) {
      const label = node('label', 'evaluation-tag');
      const checkbox = node('input'); checkbox.type = 'checkbox'; checkbox.value = tag;
      checkbox.checked = evaluation?.problems?.includes(tag) || false;
      label.append(checkbox, document.createTextNode(tagLabels[tag] || tag));
      tags.append(label);
    }
    form.append(tags);
    const best = labeledText('Mejor momento (compás o tiempo)', evaluation?.best_moment);
    const worst = labeledText('Momento que más falla (compás o tiempo)', evaluation?.worst_moment);
    const observation = labeledText('¿Qué escuchaste? ¿Qué debería cambiar?', evaluation?.observation, true);
    form.append(best.wrap, worst.wrap, observation.wrap);
    const save = node('button', 'compose-button', 'Guardar evaluación'); save.type = 'submit';
    form.append(save);
    form.addEventListener('submit', async event => {
      event.preventDefault(); save.disabled = true; shell.feedback.textContent = 'Guardando…';
      const payload = { scores: Object.fromEntries(Object.keys(criteria).map(key =>
        [key, Number(form.querySelector(`input[name="score-${key}"]:checked`)?.value)])),
      scope: scope.select.value, listening_mode: listening.select.value,
      would_use: use.select.value, confidence: confidence.select.value,
      problems: [...tags.querySelectorAll('input:checked')].map(input => input.value),
      best_moment: best.input.value, worst_moment: worst.input.value,
      observation: observation.input.value };
      try {
        const result = await api(`/api/evaluations/${job.id}`, { method: 'PUT',
          headers: { 'Content-Type': 'application/json', 'X-Pulso-Local': '1' },
          body: JSON.stringify(payload) });
        evaluationsByJob.set(job.id, result.evaluation);
        if (selectedId === job.id) renderSession(job);
        shell.feedback.textContent = 'Evaluación guardada. Quedó disponible en el informe local.';
      } catch (error) { shell.feedback.textContent = error.message; }
      finally { save.disabled = false; }
    });
    shell.body.append(form);
  } catch (error) { shell.feedback.textContent = error.message; }
}

async function openEvaluationLab(existingComparisonId = null) {
  if (exhibitionMode) return;
  const shell = dialogShell('Laboratorio musical');
  if (existingComparisonId) {
    await renderBlindComparison(shell, existingComparisonId);
    return;
  }
  shell.body.append(node('p', 'evaluation-intro',
    'Pruebas reproducibles y escucha humana. Cargar un encargo no consulta la API: solo prepara el formulario.'));
  try {
    const [bank, report] = await Promise.all([api('/api/benchmarks'), api('/api/evaluations/report')]);
    if (!shell.dialog.open) return;
    const summary = node('div', 'evaluation-summary');
    summary.append(node('strong', '', `${report.evaluated_works} obras evaluadas · ${report.blind_comparisons.length} comparaciones votadas`));
    summary.append(node('p', '', `Usaría: ${report.would_use.yes} · Tal vez: ${report.would_use.maybe} · No usaría: ${report.would_use.no}. Las medias son descriptivas, no una aprobación musical.`));
    const keys = Object.entries(report.criterion_averages).filter(([, value]) => value !== null);
    if (keys.length) summary.append(node('p', '', `Promedios: ${keys.map(([key, value]) => `${report.criteria[key]} ${value}/5`).join(' · ')}`));
    const frequentProblems = Object.entries(report.problem_counts || {}).filter(([, count]) => count > 0)
      .sort((a, b) => b[1] - a[1]).slice(0, 3);
    if (frequentProblems.length) summary.append(node('p', '',
      `Problemas más marcados: ${frequentProblems.map(([tag, count]) => `${tag.replaceAll('_', ' ')} (${count})`).join(' · ')}.`));
    const downloadReport = node('a', 'ghost-button', 'Descargar informe JSON');
    downloadReport.href = '/api/evaluations/report?download=1';
    summary.append(downloadReport); shell.body.append(summary);
    shell.body.append(node('h3', '', 'Banco de encargos'));
    const cases = labeledSelect('Ensayo', bank.cases.map(item => [item.id, item.label]));
    const caseDetail = node('p', 'evaluation-help');
    const selectedCase = () => bank.cases.find(item => item.id === cases.select.value);
    const describe = () => { const item = selectedCase(); caseDetail.textContent =
      `${item.duration_seconds / 60} min · ${item.bpm} BPM · Escuchar: ${item.listen_for.join(', ')}.`; };
    cases.select.addEventListener('change', describe); describe();
    const load = actionButton('Cargar encargo para componer', () => {
      const item = selectedCase();
      selectedBenchmarkCase = item;
      ui.prompt.value = item.prompt;
      $('#duration').value = String(item.duration_seconds);
      $('#bpm').value = String(item.bpm);
      const wasClub = $('#club-mode').checked;
      $('#club-mode').checked = item.behavior === 'club';
      $('#behavior').value = item.behavior === 'club' ? 'adaptive' : item.behavior;
      $('#prompt-count').textContent = `${ui.prompt.value.length} / 600`;
      updateClubMode($('#club-mode').checked, wasClub);
      shell.dialog.close(); ui.prompt.focus();
      showNotice('Encargo cargado. Revisá los ajustes y pulsá Componer cuando quieras; la API aún no se consultó.');
    }, 'dark-button');
    shell.body.append(cases.wrap, caseDetail, load);
    shell.body.append(node('h3', '', 'Comparación A/B a ciegas'));
    shell.body.append(node('p', 'evaluation-help',
      'Elegí dos obras con el mismo prompt, duración, tempo, enfoque y semilla. Durante la escucha solo verás A y B; se revelan después de votar. Ambas usan el mismo piano y volumen.'));
    const completed = jobs.filter(job => job.state === 'completed' && job.manifest?.fullFile);
    if (completed.length < 2) {
      shell.body.append(node('p', 'evaluation-help', 'Necesitás al menos dos obras terminadas para comparar.'));
      return;
    }
    const choices = completed.map(job => [job.id, `${job.manifest.title || 'Obra'} · ${job.id.slice(0, 8)}`]);
    const first = labeledSelect('Primera obra', choices);
    const second = labeledSelect('Segunda obra', choices, completed[1].id);
    const create = actionButton('Crear comparación ciega', async () => {
      create.disabled = true;
      try {
        const result = await api('/api/comparisons', { method: 'POST',
          headers: { 'Content-Type': 'application/json', 'X-Pulso-Local': '1' },
          body: JSON.stringify({ first_job_id: first.select.value,
            second_job_id: second.select.value }) });
        shell.body.replaceChildren();
        await renderBlindComparison(shell, result.comparison.id);
      } catch (error) { shell.feedback.textContent = error.message; create.disabled = false; }
    }, 'dark-button');
    shell.body.append(first.wrap, second.wrap, create);
  } catch (error) { shell.feedback.textContent = error.message; }
}

function showComparisonResult(shell, comparison) {
  const linkFor = side => {
    const jobId = comparison.sides[side];
    const job = jobs.find(item => item.id === jobId);
    const link = node('a', 'ghost-button',
      `Abrir ${side}: ${job?.manifest?.title || jobId} · ${job?.origin === 'auto' ? 'Auto' : 'Personal'} · ${jobId.slice(0, 8)}`);
    link.href = `?job=${encodeURIComponent(jobId)}`;
    return link;
  };
  shell.body.replaceChildren(node('h3', '', 'Resultado revelado'), linkFor('A'), linkFor('B'),
    node('p', '', `Tu voto: ${comparison.vote.winner === 'tie' ? 'Empate' :
      comparison.vote.winner === 'neither' ? 'Ninguna sirve' : comparison.vote.winner}.`));
  shell.feedback.textContent = 'Podés abrir cada obra y completar su ficha de evaluación por separado.';
}

async function renderBlindComparison(shell, id) {
  audio.stop();
  const { comparison } = await api(`/api/comparisons/${id}`);
  if (comparison.vote) { showComparisonResult(shell, comparison); return; }
  const clock = node('output', 'blind-clock', '00:00 / 00:00 · compás 1');
  const seek = node('input', 'blind-seek');
  seek.type = 'range'; seek.min = '0'; seek.max = '0'; seek.step = '0.1'; seek.value = '0';
  seek.disabled = true; seek.setAttribute('aria-label', 'Posición de reproducción A/B');
  const player = new ProductionAudio(seconds => {
    const duration = player.duration || 0;
    seek.max = String(duration);
    seek.value = String(Math.min(seconds, duration));
    const beat = player.midi?.secondsToBeat(seconds) || 0;
    const bar = Math.floor(beat / (player.midi?.beatsPerBar || 4)) + 1;
    clock.textContent = `${fmt(seconds)} / ${fmt(duration)} · compás ${bar}`;
  }, () => {});
  player.setVolume(.72);
  seek.addEventListener('input', () => player.seek(Number(seek.value)));
  let activeSide = null;
  shell.dialog.addEventListener('close', () => player.stop(), { once: true });
  shell.body.append(node('p', 'evaluation-intro',
    'Escuchá A y B con el mismo piano. Las identidades y los diagnósticos permanecen ocultos hasta votar.'));
  const controls = node('div', 'blind-controls');
  for (const side of ['A', 'B']) {
    const button = actionButton(`Escuchar ${side}`, async () => {
      try {
        if (activeSide === side && player.playing) { player.pause(); return; }
        player.stop();
        const response = await fetch(`/api/comparisons/${id}/sides/${side}.mid`, { cache: 'no-store' });
        if (!response.ok) throw new Error(`No se pudo abrir ${side}.`);
        const midi = parseMidi(await response.arrayBuffer());
        const tracks = midi.tracks.filter(track => track.notes.length).map((track, index) =>
          ({ name: `Pista ${index + 1}`, notes: track.notes, instrument: 'synth' }));
        player.setSong(midi, tracks);
        seek.disabled = false;
        clock.textContent = `00:00 / ${fmt(player.duration)} · compás 1`;
        activeSide = side;
        await player.play();
        shell.feedback.textContent = `Reproduciendo ${side}. Pulsá el mismo botón para pausar.`;
      } catch (error) { shell.feedback.textContent = error.message; }
    }, 'dark-button');
    controls.append(button);
  }
  controls.append(actionButton('Detener', () => player.stop(), 'ghost-button'));
  shell.body.append(controls, clock, seek);
  const choice = labeledSelect('¿Cuál preferís musicalmente?', [['A', 'A'], ['B', 'B'],
    ['tie', 'Empate'], ['neither', 'Ninguna sirve']]);
  const scope = labeledSelect('¿Cuánto escuchaste?', [['full', 'Ambas completas'], ['excerpt', 'Fragmentos comparables']]);
  const reason = labeledText('¿Por qué? Indicá el momento decisivo si podés.', '', true);
  reason.input.maxLength = 800;
  const vote = actionButton('Guardar voto y revelar obras', async () => {
    vote.disabled = true;
    try {
      const result = await api(`/api/comparisons/${id}/vote`, { method: 'POST',
        headers: { 'Content-Type': 'application/json', 'X-Pulso-Local': '1' },
        body: JSON.stringify({ winner: choice.select.value, scope: scope.select.value,
          listening_mode: 'piano', reason: reason.input.value }) });
      player.stop();
      showComparisonResult(shell, result.comparison);
    } catch (error) { shell.feedback.textContent = error.message; vote.disabled = false; }
  }, 'compose-button');
  shell.body.append(choice.wrap, scope.wrap, reason.wrap, vote);
}

function configureExhibition() {
  document.body.classList.add('shared-exhibition');
  document.title = 'PULSO · Exhibición compartida';
  const badge = node('span', 'exhibition-badge', 'EXHIBICIÓN COMPARTIDA');
  $('.top-actions').replaceChildren(badge);
  $('.sidebar').hidden = true;
  $('.arrangement-head').after(ui.notice);
  ui.history.hidden = true;
  ui.health.hidden = true;
  $('.arrangement-head .section-kicker').textContent = 'PARTITURA · SOLO LECTURA';
  $('#song-title').textContent = 'Abriendo la obra compartida…';
  $('#song-subtitle').textContent = 'Preparando las notas y el reproductor.';
  for (const selector of ['#preview-wav', '#full-wav']) $(selector).hidden = true;
  $('#listen-description').textContent = 'Exhibición compartida · Escuchar no modifica la obra.';
}

async function loadExhibition() {
  try {
    if (!/^[0-9a-f]{8}(?:-[0-9a-f]{4}){3}-[0-9a-f]{12}$/i.test(exhibitionId || ''))
      throw new Error('El enlace de la exhibición no es válido.');
    const { job } = await api(`/api/jobs/${encodeURIComponent(exhibitionId)}`);
    if (job?.state !== 'completed' || !job.manifest?.fullFile || !job.manifest?.tracks?.length)
      throw new Error('Esta obra todavía no está disponible como exhibición terminada.');
    await loadSong(job);
    if (!current || current.job.id !== job.id) throw new Error('No se pudo cargar la partitura compartida.');
    document.title = `${job.manifest.title || 'Obra PULSO'} · Exhibición compartida`;
  } catch (error) {
    audio.stop(); current = null; ui.play.disabled = true;
    $('#song-title').textContent = 'Exhibición no disponible';
    $('#song-subtitle').textContent = error.message;
    $('#song-subtitle').setAttribute('role', 'alert');
    ui.timeline.replaceChildren(node('div', 'timeline-empty', 'Comprueba el enlace y que el estudio que comparte esta obra esté disponible.'));
  }
}
