export const PERSONAL_ORIGIN = 'personal';
export const AUTO_ORIGIN = 'auto';

export function originForRoute(pathname) {
  return pathname === '/api/auto/jobs' ? AUTO_ORIGIN : PERSONAL_ORIGIN;
}

export function originForJob(metadata) {
  return metadata?.origin === AUTO_ORIGIN ? AUTO_ORIGIN : PERSONAL_ORIGIN;
}
