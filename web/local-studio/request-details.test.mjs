import assert from 'node:assert/strict';
import { readFileSync } from 'node:fs';
import vm from 'node:vm';
import { test } from 'node:test';

const app = readFileSync(new URL('./app.js', import.meta.url), 'utf8');
const source = app.slice(app.indexOf('function requestDetails('), app.indexOf('function renderSession('));
const details = vm.runInNewContext(source + '; requestDetails');

test('session shows each saved Club horizon rather than a fixed 64-bar description', () => {
  for (const pace of ['8-16', '16-32', '32-64']) {
    const rows = Object.fromEntries(details({ behavior: 'club', club_layer_pace: pace, club_variety: false }));
    assert.ok(rows['Evolución de capas Club'].startsWith(pace.replace('-', '–') + ' compases'));
    assert.equal(rows['Club: recurso principal libre'], 'Desactivado');
  }
});
test('automatic and legacy requests are not represented as a selected 64-bar horizon', () => {
  assert.match(Object.fromEntries(details({ behavior: 'club', club_layer_pace: 'auto' }))['Evolución de capas Club'], /Automática/);
  assert.match(Object.fromEntries(details({ behavior: 'club' }))['Evolución de capas Club'], /No registrado/);
  assert.match(Object.fromEntries(details({ behavior: 'adaptive', club_layer_pace: '32-64' }))['Evolución de capas Club'], /No aplica/);
});
test('request flags and source references are read without inventing missing values', () => {
  const rows = Object.fromEntries(details({ first_pass_audition: false, allow_chromatic: true, resume_from_job: 'source-id' }));
  assert.equal(rows['Primera pasada sin revisión global'], 'Desactivado');
  assert.equal(rows.Cromatismos, 'Activado');
  assert.equal(rows['Plan de frases entre voces'], 'No registrado');
  assert.equal(rows['Reanudación desde'], 'source-id');
});
