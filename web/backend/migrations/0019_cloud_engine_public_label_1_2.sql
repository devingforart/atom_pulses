-- Public name correction, not a new composer. Keep old requests resumable.
ALTER TABLE cloud_jobs DROP CONSTRAINT cloud_jobs_engine_version_valid;
ALTER TABLE cloud_jobs ADD CONSTRAINT cloud_jobs_engine_version_valid
    CHECK (engine_version IN ('v1', 'v1.1', '0.1.3', '1.2'));
ALTER TABLE cloud_jobs ALTER COLUMN engine_version SET DEFAULT '1.2';
