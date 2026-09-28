CREATE TABLE cloud_jobs (
    id UUID PRIMARY KEY,
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    idempotency_key TEXT NOT NULL,
    prompt TEXT NOT NULL,
    duration_seconds INTEGER NOT NULL CHECK (duration_seconds BETWEEN 30 AND 900),
    bpm DOUBLE PRECISION NOT NULL CHECK (bpm BETWEEN 60 AND 180),
    behavior TEXT NOT NULL CHECK (behavior IN ('adaptive', 'hypnotic', 'narrative')),
    seed BIGINT NOT NULL,
    status TEXT NOT NULL CHECK (status IN ('queued', 'running', 'completed', 'failed', 'cancelled')),
    stage TEXT NOT NULL DEFAULT 'queued',
    completed_steps INTEGER NOT NULL DEFAULT 0,
    total_steps INTEGER NOT NULL DEFAULT 0,
    attempt INTEGER NOT NULL DEFAULT 0,
    lease_until BIGINT,
    result_manifest JSONB,
    error_code TEXT,
    cancel_requested BOOLEAN NOT NULL DEFAULT FALSE,
    created_at BIGINT NOT NULL,
    updated_at BIGINT NOT NULL,
    UNIQUE(user_id, idempotency_key)
);
CREATE INDEX cloud_jobs_queue_idx ON cloud_jobs(status, created_at) WHERE status IN ('queued', 'running');
CREATE INDEX cloud_jobs_user_idx ON cloud_jobs(user_id, created_at DESC);
