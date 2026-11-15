-- Preserve all existing engine assignments and allow the new isolated worker.
ALTER TABLE cloud_jobs DROP CONSTRAINT cloud_jobs_engine_version_valid;
ALTER TABLE cloud_jobs ADD CONSTRAINT cloud_jobs_engine_version_valid
    CHECK (engine_version IN ('v1', 'v1.1', '0.1.3'));
ALTER TABLE cloud_jobs ALTER COLUMN engine_version SET DEFAULT '0.1.3';
