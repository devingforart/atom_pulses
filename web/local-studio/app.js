const form = document.querySelector('#compose-form');
const jobsElement = document.querySelector('#jobs');
const notice = document.querySelector('#notice');
const submit = document.querySelector('#submit');
const statusElement = document.querySelector('#health');
const prompt = document.querySelector('#prompt');
const statusNames = { queued: 'EN COLA', running: 'COMPONIENDO', completed: 'LISTA', failed: 'FALLIDA', interrupted: 'INTERRUMPIDA', cancelled: 'CANCELADA' };

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
  const actions = element('div', 'job-actions');
  actions.append(link(job.id, 'job.json', 'SOLICITUD JSON'));
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
