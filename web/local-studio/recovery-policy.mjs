export const AUTOMATIC_RECOVERY_MAX_SECONDS = 600;
export const AUTOMATIC_RECOVERY_MAX_USD = 20;

export function canAutomaticallyRecover(job, hasExactBlueprint) {
  if (!job || job.state !== 'failed' || !hasExactBlueprint ||
      !job.checkpoint?.file || job.manifest ||
      !job.request?.ai_sovereign || job.request.proof_mode ||
      job.request.automatic_recovery_attempt ||
      job.request.resume_from_job) return false;
  return !/no credits remaining|insufficient_quota|créditos disponibles|invalid api key|authentication|unauthorized/i
    .test(job.error || '');
}

export function automaticRecoveryRequest(job, sourceId, now = new Date()) {
  const request = { ...job.request, resume_from_job: sourceId,
    automatic_recovery_attempt: 1, recovery_source_job: sourceId,
    created_at: now.toISOString() };
  delete request.resume_checkpoint_file;
  return request;
}
