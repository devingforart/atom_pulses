ALTER TABLE cloud_jobs ADD COLUMN allow_chromatic BOOLEAN NOT NULL DEFAULT FALSE;

CREATE TABLE cloud_exhibitions (
    token_hash TEXT PRIMARY KEY,
    job_id UUID NOT NULL REFERENCES cloud_jobs(id) ON DELETE CASCADE,
    created_at BIGINT NOT NULL
);
CREATE INDEX cloud_exhibitions_job_idx ON cloud_exhibitions(job_id);
