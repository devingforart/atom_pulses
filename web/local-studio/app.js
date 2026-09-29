const form = document.querySelector('#compose-form');
const jobsElement = document.querySelector('#jobs');
const notice = document.querySelector('#notice');
const submit = document.querySelector('#submit');
const statusElement = document.querySelector('#health');
const prompt = document.querySelector('#prompt');
const statusNames = { queued: 'EN COLA', running: 'COMPONIENDO', completed: 'LISTA', failed: 'FALLIDA', interrupted: 'INTERRUMPIDA', cancelled: 'CANCELADA' };
const phaseNames = { blueprint: 'Diseño', writing: 'Escritura', recovery: 'Recuperación', validation: 'Validación', rendering: 'MIDI', ready: 'Cierre' };
function duration(ms) { const seconds = Math.max(0, Math.round((Number(ms) || 0) / 1000)); return `${Math.floor(seconds / 60)}m ${String(seconds % 60).padStart(2, '0')}s`; }
function count(value) { return new Intl.NumberFormat('es-AR').format(Number(value) || 0); }

function showNotice(message) { notice.textContent = message; notice.hidden = !message; }
function element(tag, className, text) { const node = document.createElement(tag); if (className) node.className = className; if (text !== undefined) node.textContent = text; return node; }
function link(id, filename, label, className = 'download') { const node = element('a', className, label); node.href = `/api/jobs/${id}/files/${encodeURIComponent(filename)}`; node.download = filename; return node; }
async function api(url, options = {}) { const response = await fetch(url, { cache: 'no-store', ...options }); const result = await response.json(); if (!response.ok) throw new Error(result.error || `Error HTTP ${response.status}`); return result; }

function renderJob(job) {
  const card = element('article', 'job');
  const header = element('div', 'job-head');
  const title = element('div');
  title.append(element('h3', '', job.manifest?.title || 'Nueva composición'));
  const time = new Date(job.request.created_at).toLocaleString('es-AR', { dateStyle: 'short', timeStyle: 'short' });
  title.append(element('div', 'job-meta', `${time} · ${job.request.duration_seconds} s · ${job.request.bpm} BPM · semilla ${job.request.seed}`));
  header.append(title, element('div', `status ${job.state}`, statusNames[job.state] || job.state.toUpperCase()));
  card.append(header, element('p', 'job-prompt', job.request.prompt));
  if (job.state === 'running' || job.state === 'queued') {
    const elapsed = Math.max(0, Math.round((Date.now() - new Date(job.request.created_at).getTime()) / 1000));
    const mins = Math.floor(elapsed / 60);
    const secs = String(elapsed % 60).padStart(2, '0');
    card.append(element('div', 'job-progress', `${job.stage || 'Iniciando'} · ${mins}:${secs}${job.total ? ` · ${job.completed}/${job.total}` : ''}`));
    const bar = element('div', 'progress-track');
    const fill = element('i'); fill.style.width = job.total ? `${Math.min(100, (job.completed / job.total) * 100)}%` : '16%';
    bar.append(fill); card.append(bar);
  }
  if (job.error) card.append(element('p', 'notice', job.error));
  if (job.telemetry) {
    const stats = element('div', 'telemetry');
    stats.append(element('div', 'telemetry-title', 'TRAZABILIDAD DE LA OBRA'));
    const grid = element('div', 'telemetry-grid');
    const values = [
      ['Tiempo real', duration(job.telemetry.wallMs)],
      ['Llamadas IA', String(job.telemetry.calls)],
      ['Entrada', `${count(job.telemetry.inputTokens)} tokens`],
      ['Salida', `${count(job.telemetry.outputTokens)} tokens`],
      ['Razonamiento', `${count(job.telemetry.reasoningTokens)} tokens`],
      ['Caché', `${count(job.telemetry.cachedInputTokens)} tokens`],
      ['Costo estimado', job.telemetry.pricedCalls ? `USD ${Number(job.telemetry.estimatedUsd).toFixed(4)}` : 'sin datos'],
      ['Incompletas', String(job.telemetry.incompleteCalls)],
      ['Timeouts', String(job.telemetry.timedOutCalls)],
      ['Recuperaciones', String(job.telemetry.recoveryEvents)],
    ];
    for (const [label, value] of values) { const item = element('div', 'telemetry-item'); item.append(element('span', '', label), element('strong', '', value)); grid.append(item); }
    stats.append(grid);
    const phases = Object.entries(job.telemetry.phaseMs || {});
    if (phases.length) stats.append(element('p', 'telemetry-phases', phases.map(([name, ms]) => `${phaseNames[name] || name}: ${duration(ms)}`).join(' · ')));
    if (!job.telemetry.calls) stats.append(element('p', 'telemetry-note', 'Sin métricas por llamada: esta obra pudo haber iniciado con el worker anterior.'));
    else if (job.telemetry.callsWithUsage < job.telemetry.calls) stats.append(element('p', 'telemetry-note', `${job.telemetry.calls - job.telemetry.callsWithUsage} llamada(s) sin datos de uso devueltos por la API; los tokens mostrados son un mínimo medido.`));
    if (job.telemetry.pricedCalls) stats.append(element('p', 'telemetry-note', 'Costo orientativo según tarifa Terra del 29/09/2026. Puede no incluir respuestas sin uso reportado ni ajustes de facturación.'));
    card.append(stats);
  }
  const actions = element('div', 'job-actions');
  actions.append(link(job.id, 'job.json', 'SOLICITUD JSON'));
  const traceLink = element('a', 'download', '↓ TRAZA JSON'); traceLink.href = `/api/jobs/${job.id}/trace?download=1`; traceLink.download = `pulso-trace-${job.id}.json`; actions.append(traceLink);
  if (job.manifest) {
    actions.append(link(job.id, 'manifest.json', 'MANIFIESTO'));
    actions.append(link(job.id, job.manifest.fullFile, '↓ OBRA COMPLETA MIDI'));
  }
  if (job.state === 'running' || job.state === 'queued') {
    const cancel = element('button', 'cancel', 'DETENER PROCESO'); cancel.type = 'button';
    cancel.addEventListener('click', async () => { if (!window.confirm('¿Detener el proceso local? Una solicitud remota ya aceptada podría seguir consumiendo créditos.')) return; try { const result = await api(`/api/jobs/${job.id}/cancel`, { method: 'POST', headers: { 'X-Pulso-Local': '1' } }); showNotice(result.message); await refresh(); } catch (error) { showNotice(error.message); } });
    actions.append(cancel);
  }
  card.append(actions);
  if (job.manifest?.tracks?.length) {
    const tracks = element('div', 'tracks');
    for (const track of job.manifest.tracks) {
      const row = element('div', 'track');
      const label = element('span', '', track.name); label.title = track.name;
      const meta = element('small', '', `${track.notes} notas`);
      const download = link(job.id, track.filename, 'MIDI ↓', '');
      row.append(label, meta, download); tracks.append(row);
    }
    card.append(tracks);
  }
  return card;
}

async function refresh() {
  try {
    const [status, listing] = await Promise.all([api('/api/status'), api('/api/jobs')]);
    statusElement.className = `health ${status.workerReady && status.apiKeyConfigured ? 'ok' : 'warn'}`;
    statusElement.textContent = status.workerReady && status.apiKeyConfigured ? 'Motor local listo · La IA solo se activa cuando iniciás una composición.' : `Preparación pendiente: ${status.workerReady ? '' : 'falta el ejecutable generador. '}${status.apiKeyConfigured ? '' : 'falta OPENAI_API_KEY en este proceso.'}`;
    submit.disabled = Boolean(status.activeJobId) || !status.workerReady || !status.apiKeyConfigured;
    document.querySelector('#job-count').textContent = `${listing.jobs.length} SESIONES`;
    jobsElement.replaceChildren();
    if (!listing.jobs.length) {
      const empty = element('div', 'empty');
      empty.append(element('span', 'empty-mark', '◌'), element('h3', '', 'El lienzo está listo.'), element('p', '', 'Cuando compongas, tus obras aparecerán aquí con su progreso y pistas exportables.'));
      jobsElement.append(empty);
    } else for (const job of listing.jobs) jobsElement.append(renderJob(job));
  } catch (error) { statusElement.className = 'health warn'; statusElement.textContent = `No se pudo conectar con el estudio local: ${error.message}`; submit.disabled = true; }
}

prompt.addEventListener('input', () => { document.querySelector('#prompt-count').textContent = String(prompt.value.length); });
document.querySelector('#refresh').addEventListener('click', refresh);
form.addEventListener('submit', async event => {
  event.preventDefault(); showNotice(''); submit.disabled = true;
  const payload = { prompt: prompt.value, duration_seconds: Number(document.querySelector('#duration').value), bpm: Number(document.querySelector('#bpm').value), behavior: document.querySelector('#behavior').value, seed: document.querySelector('#seed').value.trim() };
  try { await api('/api/jobs', { method: 'POST', headers: { 'Content-Type': 'application/json', 'X-Pulso-Local': '1' }, body: JSON.stringify(payload) }); await refresh(); }
  catch (error) { showNotice(error.message); submit.disabled = false; }
});
refresh();
setInterval(refresh, 3000);
