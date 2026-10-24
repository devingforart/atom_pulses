import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { SourceTextModule, SyntheticModule, createContext } from 'node:vm';
import { JSDOM } from '../web/frontend/node_modules/jsdom/lib/api.js';
import * as midi from '../web/shared/midi.mjs';
import * as sounds from '../web/shared/sound-palette.mjs';
import { instrumentFor } from '../web/shared/audio-engine.mjs';
import { criteria, problemTags } from '../web/local-studio/evaluation-store.mjs';

const id = '11111111-1111-4111-8111-111111111111';
const request = { prompt: 'Private production prompt', seed: '123', duration_seconds: 60,
  bpm: 120, created_at: '2026-10-21T12:00:00Z', ai_sovereign: true };
const job = { id, state: 'completed', origin: 'personal', request,
  manifest: { title: 'Una obra compartida', fullFile: 'full-song.mid', bpm: 120,
    bars: 1, key: 'C major', tracks: [{ name: 'Melodía', filename: 'lead.mid', instrument: {} }] } };
const bytes = new Uint8Array([77,84,104,100,0,0,0,6,0,0,0,1,0,96,
  77,84,114,107,0,0,0,12,0,144,60,90,96,128,60,0,0,255,47,0]);
const flush = async () => { for (let i = 0; i < 15; i++) await new Promise(resolve => setImmediate(resolve)); };

async function studio(query, item = job) {
  const dom = new JSDOM(readFileSync(new URL('../web/local-studio/index.html', import.meta.url), 'utf8'),
    { url: `http://localhost:4214/${query}`, pretendToBeVisual: true });
  const { window } = dom;
  window.HTMLCanvasElement.prototype.getContext = () => ({ scale() {}, fillRect() {} });
  window.HTMLDialogElement.prototype.showModal = function () { this.open = true; };
  window.HTMLDialogElement.prototype.close = function () { this.open = false; this.dispatchEvent(new window.Event('close')); };
  const calls = [], intervals = [], players = [];
  class Audio {
    constructor(onPosition, onStop) {
      Object.assign(this, { mode: 'piano', duration: 1, playing: false,
        muted: new Set(), soloed: new Set(), onPosition, onStop }); players.push(this);
    }
    stop() { this.playing = false; this.onPosition(0); this.onStop(); }
    setSong(score, tracks) { this.midi = score; this.tracks = tracks; }
    setVolume(value) { this.volume = value; }
    async play() { this.playing = true; }
    pause() { this.playing = false; }
    seek(value) { this.onPosition(value); }
  }
  const context = createContext({ document: window.document, window, location: window.location,
    navigator: window.navigator, localStorage: window.localStorage, Option: window.Option,
    URL, URLSearchParams, Intl, console, confirm: () => true,
    setTimeout, clearTimeout, setInterval: fn => { intervals.push(fn); return intervals.length; },
    fetch: async (url, options = {}) => {
      calls.push({ url, options });
      if (url === '/api/status') return { ok: true, json: async () => ({ workerReady: true, apiKeyConfigured: true }) };
      if (url === '/api/jobs' && options.method === 'POST')
        return { ok: true, json: async () => ({ job: { id: item.id } }) };
      if (url === '/api/jobs') return { ok: true, json: async () => ({ jobs: [item] }) };
      if (url === `/api/jobs/${id}`) return { ok: true, json: async () => ({ job: item }) };
      if (url === `/api/evaluations/${id}` && !options.method)
        return { ok: true, json: async () => ({ evaluation: null, criteria, problemTags }) };
      if (url === `/api/evaluations/${id}` && options.method === 'PUT')
        return { ok: true, json: async () => ({ evaluation: {
          ...JSON.parse(options.body), updated_at: '2026-10-23T12:00:00Z' } }) };
      if (url === '/api/benchmarks') return { ok: true, json: async () => ({ cases: [{
        id: 'test', label: 'Ensayo de prueba', prompt: 'Una idea breve que evoluciona',
        duration_seconds: 180, bpm: 122, behavior: 'club', listen_for: ['Motivo'] }] }) };
      if (url === '/api/evaluations/report') return { ok: true, json: async () => ({
        evaluated_works: 0, blind_comparisons: [], would_use: { yes: 0, maybe: 0, no: 0 },
        criterion_averages: {}, criteria }) };
      if (url === '/api/comparisons/22222222-2222-4222-8222-222222222222')
        return { ok: true, json: async () => ({ comparison: {
          id: '22222222-2222-4222-8222-222222222222', voted: false, sides: ['A', 'B'] } }) };
      if (url.endsWith('.mid')) return { ok: true, arrayBuffer: async () => bytes.buffer };
      throw new Error(`Unexpected network request: ${url}`);
    } });
  const imports = {
    './midi.mjs': midi, './audio-engine.mjs': { instrumentFor },
    './production-audio.mjs': { ProductionAudio: Audio }, './sound-palette.mjs': sounds,
  };
  const source = new SourceTextModule(readFileSync(new URL('../web/local-studio/app.js', import.meta.url), 'utf8'), { context });
  await source.link(specifier => {
    const exports = imports[specifier];
    return new SyntheticModule(Object.keys(exports), function () {
      for (const [name, value] of Object.entries(exports)) this.setExport(name, value);
    }, { context });
  });
  await source.evaluate(); await flush();
  return { dom, document: window.document, calls, intervals, players };
}

test('shared exhibition opens only the requested completed score and plays without polling or writes', async () => {
  const result = await studio(`?share=${id}`);
  const { document, calls, intervals, players } = result;
  assert.equal(document.querySelector('#song-title').textContent, job.manifest.title);
  assert.equal(document.querySelectorAll('.track-row').length, 1);
  assert.equal(document.querySelector('.sidebar').hidden, true);
  assert.match(document.querySelector('.exhibition-badge').textContent, /EXHIBICIÓN COMPARTIDA/);
  assert.equal(intervals.length, 0);
  assert.equal(calls.some(call => call.url === '/api/jobs' || call.url === '/api/status'), false);
  assert.equal(calls.some(call => call.options.method && call.options.method !== 'GET'), false);
  document.querySelector('#play').click(); await flush();
  assert.equal(players[0].playing, true);
  document.querySelector('#stop').click();
  assert.equal(players[0].playing, false);
  result.dom.window.close();
});

test('invalid or unfinished exhibitions show an error without exposing the studio or provisional MIDI', async () => {
  const invalid = await studio('?share=not-a-job');
  assert.match(invalid.document.querySelector('#song-title').textContent, /no disponible/);
  assert.equal(invalid.calls.length, 0);
  invalid.dom.window.close();
  const running = await studio(`?share=${id}`, { ...job, state: 'running', manifest: null });
  assert.match(running.document.querySelector('#song-subtitle').textContent, /todavía no está disponible/);
  assert.equal(running.calls.length, 1);
  assert.equal(running.document.querySelector('#play').disabled, true);
  running.dom.window.close();
});

test('local evaluator saves a complete listening sheet without changing the composition', async () => {
  const result = await studio('', job);
  const { document, calls } = result;
  document.querySelector('#evaluate').click(); await flush();
  assert.equal(document.querySelectorAll('.score-row').length, Object.keys(criteria).length);
  for (const key of Object.keys(criteria))
    document.querySelector(`input[name="score-${key}"][value="4"]`).checked = true;
  document.querySelector('.evaluation-form').dispatchEvent(new result.dom.window.Event('submit', { bubbles: true, cancelable: true }));
  await flush();
  const saved = calls.find(call => call.url === `/api/evaluations/${id}` && call.options.method === 'PUT');
  assert.ok(saved);
  assert.equal(JSON.parse(saved.options.body).scores.melody, 4);
  assert.match(document.querySelector('.share-feedback').textContent, /guardada/i);
  assert.equal(calls.some(call => call.url === '/api/jobs' && call.options.method === 'POST'), false);
  result.dom.window.close();
});

test('benchmark bank only loads controls and never launches a paid composition', async () => {
  const result = await studio('', job);
  const { document, calls } = result;
  document.querySelector('#lab').click(); await flush();
  assert.match(document.querySelector('.evaluation-dialog').textContent, /Banco de encargos/);
  [...document.querySelectorAll('.evaluation-dialog button')]
    .find(button => button.textContent === 'Cargar encargo para componer').click();
  assert.equal(document.querySelector('#prompt').value, 'Una idea breve que evoluciona');
  assert.equal(document.querySelector('#duration').value, '180');
  assert.equal(document.querySelector('#club-mode').checked, true);
  assert.equal(document.querySelector('#first-pass-audition').checked, true);
  assert.equal(calls.some(call => call.options.method === 'POST'), false);
  result.dom.window.close();
});

test('completed first pass offers an explicit paid review preserving its exact request', async () => {
  const source = { ...job, request: { ...request, behavior: 'club',
    benchmark_case_id: 'club_nucleus', phrase_architecture_experiment: true,
    first_pass_audition: true, allow_chromatic: false },
  manifest: { ...job.manifest, first_pass_audition: true } };
  const result = await studio(`?job=${id}`, source);
  const button = [...result.document.querySelectorAll('#session-content button')]
    .find(item => item.textContent === 'Revisar armonía y cierre con IA');
  assert.ok(button);
  button.click(); await flush();
  const call = result.calls.find(item => item.url === '/api/jobs' && item.options.method === 'POST');
  assert.ok(call);
  const payload = JSON.parse(call.options.body);
  assert.equal(payload.review_from_job, id);
  assert.equal(payload.first_pass_audition, false);
  assert.equal(payload.seed, request.seed);
  assert.equal(payload.benchmark_case_id, 'club_nucleus');
  result.dom.window.close();
});

test('a direct comparison link opens blinded A/B without generating another work', async () => {
  const result = await studio('?compare=22222222-2222-4222-8222-222222222222');
  const dialog = result.document.querySelector('.evaluation-dialog');
  assert.ok(dialog?.open);
  assert.match(dialog.textContent, /Escuchar A/);
  assert.match(dialog.textContent, /Escuchar B/);
  assert.match(dialog.textContent, /compás/);
  assert.ok(dialog.querySelector('[aria-label="Posición de reproducción A/B"]'));
  assert.equal(dialog.textContent.includes('Una obra compartida'), false);
  assert.equal(result.calls.some(call => call.options.method === 'POST'), false);
  result.dom.window.close();
});

test('Share opens a dialog with a read-only exhibition link, without altering the job', async () => {
  const result = await studio(`?job=${id}`);
  const button = result.document.querySelector('[aria-label="Compartir obra"]');
  assert.equal(button.disabled, false);
  button.click();
  const dialog = result.document.querySelector('dialog');
  assert.equal(dialog.open, true);
  assert.equal(dialog.querySelector('input').readOnly, true);
  assert.equal(dialog.querySelector('input').value, `http://localhost:4214/?share=${id}`);
  assert.equal(dialog.querySelector('a').rel, 'noopener noreferrer');
  assert.match(dialog.textContent, /No publica la obra en Internet/);
  assert.equal(result.calls.some(call => call.options.method && call.options.method !== 'GET'), false);
  dialog.querySelector('button').click();
  assert.equal(result.document.querySelector('dialog'), null);
  result.dom.window.close();
});
