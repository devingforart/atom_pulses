// Optional Windows/Edge smoke test for the already-running local studio.
// Reads saved MIDI only. It never submits a composition or calls OpenAI.
import { spawn } from 'node:child_process';
import { mkdtempSync, rmSync, existsSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';

const edge = process.env.PULSO_TEST_BROWSER || 'C:\\Program Files (x86)\\Microsoft\\Edge\\Application\\msedge.exe';
if (!existsSync(edge)) throw new Error('No se encontró Microsoft Edge; definí PULSO_TEST_BROWSER.');
const studio = process.env.PULSO_TEST_STUDIO_URL || 'http://127.0.0.1:4177/';
const port = 9341;
const profile = mkdtempSync(path.join(os.tmpdir(), 'pulso-suite-smoke-'));
const child = spawn(edge, ['--headless=new', '--disable-gpu', '--no-first-run', '--no-default-browser-check', '--autoplay-policy=no-user-gesture-required', `--user-data-dir=${profile}`, `--remote-debugging-port=${port}`, studio], { windowsHide: true, stdio: 'ignore' });
const wait = ms => new Promise(resolve => setTimeout(resolve, ms));
let socket;
try {
  let page;
  for (let i = 0; i < 80; i++) {
    try { const pages = await (await fetch(`http://127.0.0.1:${port}/json`)).json(); page = pages.find(item => item.type === 'page' && item.url.startsWith(studio)); if (page) break; }
    catch { /* Edge is starting. */ }
    await wait(100);
  }
  if (!page) throw new Error('Edge no abrió la suite.');
  socket = new WebSocket(page.webSocketDebuggerUrl);
  await new Promise((resolve, reject) => { socket.onopen = resolve; socket.onerror = reject; });
  let nextId = 0;
  const pending = new Map(), exceptions = [];
  socket.onmessage = event => {
    const message = JSON.parse(event.data);
    if (message.method === 'Runtime.exceptionThrown') exceptions.push(message.params.exceptionDetails.text);
    if (message.id && pending.has(message.id)) { const { resolve, reject } = pending.get(message.id); pending.delete(message.id); message.error ? reject(new Error(message.error.message)) : resolve(message.result); }
  };
  const command = (method, params = {}) => new Promise((resolve, reject) => { const id = ++nextId; pending.set(id, { resolve, reject }); socket.send(JSON.stringify({ id, method, params })); });
  const evaluate = async expression => { const result = await command('Runtime.evaluate', { expression, returnByValue: true, awaitPromise: true }); if (result.exceptionDetails) throw new Error(result.exceptionDetails.text); return result.result.value; };
  await command('Runtime.enable');
  let tracks = 0;
  for (let i = 0; i < 80; i++) { tracks = await evaluate("document.querySelectorAll('.track-row').length"); if (tracks) break; await wait(100); }
  if (!tracks) throw new Error('No se dibujaron pistas MIDI.');
  const durationDefault = await evaluate("document.querySelector('#duration').value");
  if (durationDefault !== '32') throw new Error(`La prueba corta no es la seleccion predeterminada: ${durationDefault}`);
  const proofDefault = await evaluate("document.querySelector('#proof-mode').checked");
  if (!proofDefault) throw new Error('La prueba de tres pistas no esta activa por defecto.');
  const before = await evaluate("document.querySelector('#time-display').textContent");
  await evaluate("document.querySelector('#play').click()");
  await wait(1400);
  const after = await evaluate("document.querySelector('#time-display').textContent");
  if (before === after) throw new Error(`El transporte no avanzó: ${after}`);
  await evaluate("document.querySelector('.track-bottom button').click()");
  const muted = await evaluate("document.querySelector('.track-bottom button').getAttribute('aria-pressed')");
  if (muted !== 'true') throw new Error('Mute no respondió.');
  await evaluate("document.querySelector('.track-bottom button:nth-child(2)').click()");
  const soloed = await evaluate("document.querySelector('.track-bottom button:nth-child(2)').getAttribute('aria-pressed')");
  if (soloed !== 'true') throw new Error('Solo no respondió.');
  await evaluate("document.querySelector('#stop').click()");
  const stopped = await evaluate("document.querySelector('#time-display').textContent");
  if (!stopped.startsWith('00:00')) throw new Error(`Stop no volvió al inicio: ${stopped}`);
  if (exceptions.length) throw new Error(`Excepción del navegador: ${exceptions[0]}`);
  console.log(JSON.stringify({ tracks, durationDefault, proofDefault, before, after, muted, soloed, stopped, browserExceptions: 0 }));
} finally {
  socket?.close();
  child.kill();
  await wait(350);
  const tempRoot = path.resolve(os.tmpdir()) + path.sep;
  if (path.resolve(profile).startsWith(tempRoot)) {
    try { rmSync(profile, { recursive: true, force: true }); } catch { /* Edge may still be closing. */ }
  }
}
