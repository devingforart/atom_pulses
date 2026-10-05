import { existsSync, readdirSync } from 'node:fs';
import os from 'node:os';
import path from 'node:path';
import { fileURLToPath } from 'node:url';

const projectRoot = path.resolve(path.dirname(fileURLToPath(import.meta.url)), '..', '..');
export const dataRoot = process.env.PULSO_LOCAL_DATA_DIR || path.join(projectRoot, 'build', 'local-studio-data');
export const legacyDataRoot = path.join(process.env.LOCALAPPDATA || os.tmpdir(), 'Pulso', 'LocalStudio');
export const jobRoots = [...new Set([dataRoot, legacyDataRoot].map(root => path.join(root, 'jobs')))];
export const jobsRoot = jobRoots[0];
const jobIdPattern = /^[0-9a-f]{8}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{4}-[0-9a-f]{12}$/i;

export function jobDirectory(id, roots = jobRoots) {
  if (!jobIdPattern.test(id)) return null;
  for (const root of roots) {
    const directory = path.join(root, id);
    if (existsSync(directory)) return directory;
  }
  return path.join(roots[0], id);
}

export function listJobIds(roots = jobRoots) {
  const ids = new Set();
  for (const root of roots) {
    if (!existsSync(root)) continue;
    for (const entry of readdirSync(root, { withFileTypes: true })) {
      if (entry.isDirectory() && jobIdPattern.test(entry.name)) ids.add(entry.name);
    }
  }
  return [...ids];
}
