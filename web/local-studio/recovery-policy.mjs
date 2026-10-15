export const AUTOMATIC_RECOVERY_MAX_SECONDS = 600;
export const AUTOMATIC_RECOVERY_MAX_USD = 20;
export const MAX_CHECKPOINT_RECOVERIES = 3;

export function canAutomaticallyRecover(job, hasExactBlueprint) {
  const attempt = job?.request?.automatic_recovery_attempt || 0;
  if (!job || job.state !== 'failed' || !hasExactBlueprint ||
      !job.checkpoint?.file || job.manifest ||
      !job.request?.ai_sovereign || job.request.proof_mode || job.request.first_pass_audition ||
      job.request.review_from_job ||
      !Number.isInteger(attempt) || attempt >= MAX_CHECKPOINT_RECOVERIES ||
      (job.request.resume_from_job && attempt === 0)) return false;
  return !/no credits remaining|insufficient_quota|créditos disponibles|invalid api key|authentication|unauthorized/i
    .test(job.error || '');
}

export function automaticRecoveryRequest(job, sourceId, now = new Date()) {
  const nextAttempt = (job.request.automatic_recovery_attempt || 0) + 1;
  const request = { ...job.request, resume_from_job: sourceId,
    automatic_recovery_attempt: nextAttempt,
    recovery_source_job: job.request.recovery_source_job || sourceId,
    created_at: now.toISOString() };
  delete request.resume_checkpoint_file;
  return request;
}
