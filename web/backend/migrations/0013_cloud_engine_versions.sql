ALTER TABLE cloud_jobs ADD COLUMN engine_version TEXT NOT NULL DEFAULT 'v1';
ALTER TABLE cloud_jobs ADD CONSTRAINT cloud_jobs_engine_version_valid
    CHECK (engine_version IN ('v1', 'v1.1'));
