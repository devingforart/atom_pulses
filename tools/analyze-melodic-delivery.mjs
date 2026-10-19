import fs from 'node:fs';
import path from 'node:path';
import { fileURLToPath } from 'node:url';
import { parseMidi } from '../web/shared/midi.mjs';
import { analyzeMelodicDelivery } from '../web/shared/melodic-delivery-audit.mjs';

const root = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..');
const id = process.argv[2];
if (!id) throw new Error('Usage: node tools/analyze-melodic-delivery.mjs <job-id> [--output file]');
const directory = /^[0-9a-f-]{36}$/i.test(id) ? path.join(root, 'build/local-studio-data/jobs', id) : path.resolve(id);
const read = name => JSON.parse(fs.readFileSync(path.join(directory, name), 'utf8'));
const manifest = read('manifest.json');
const report = { jobId: path.basename(directory), title: manifest.title,
  ...analyzeMelodicDelivery(parseMidi(fs.readFileSync(path.join(directory, manifest.fullFile || 'full-song.mid'))),
    read(manifest.planFile || 'composition-plan.json'),
    manifest.phraseArchitectureFile ? read(manifest.phraseArchitectureFile) : null) };
const output = process.argv.indexOf('--output');
if (output >= 0) {
  if (!process.argv[output + 1]) throw new Error('--output requires a filename');
  fs.writeFileSync(path.resolve(process.argv[output + 1]), JSON.stringify(report, null, 2) + '\n');
  console.log(JSON.stringify({ jobId: report.jobId, title: report.title, tracks: report.tracks.length }));
} else console.log(JSON.stringify(report, null, 2));
