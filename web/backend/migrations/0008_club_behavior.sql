-- Preserve old requests; Club uses the same immutable, checkpointed behaviour.
ALTER TABLE cloud_jobs DROP CONSTRAINT cloud_jobs_behavior_check;
ALTER TABLE cloud_jobs ADD CONSTRAINT cloud_jobs_behavior_check
    CHECK (behavior IN ('adaptive', 'hypnotic', 'narrative', 'club'));
