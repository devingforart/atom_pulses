import test from 'node:test';
import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import { SourceTextModule, SyntheticModule, createContext } from 'node:vm';
import { JSDOM } from '../web/frontend/node_modules/jsdom/lib/api.js';
import * as midi from '../web/shared/midi.mjs';
import * as sounds from '../web/shared/sound-palette.mjs';
import { instrumentFor } from '../web/shared/audio-engine.mjs';

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
      if (url === '/api/jobs') return { ok: true, json: async () => ({ jobs: [item] }) };
      if (url === `/api/jobs/${id}`) return { ok: true, json: async () => ({ job: item }) };
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
