CREATE INDEX cloud_jobs_completed_usage_idx ON cloud_jobs(user_id, updated_at DESC) WHERE status='completed';
CREATE TABLE demo_feedback (
    id UUID PRIMARY KEY,
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    request_key UUID NOT NULL,
    version INTEGER NOT NULL DEFAULT 1,
    completed_works BIGINT NOT NULL,
    last_job_id UUID REFERENCES cloud_jobs(id) ON DELETE SET NULL,
    overall SMALLINT NOT NULL CHECK (overall BETWEEN 1 AND 5),
    musicality SMALLINT NOT NULL CHECK (musicality BETWEEN 1 AND 5),
    ease_of_use SMALLINT NOT NULL CHECK (ease_of_use BETWEEN 1 AND 5),
    strengths TEXT NOT NULL CHECK (char_length(strengths) BETWEEN 1 AND 2000),
    improvements TEXT NOT NULL CHECK (char_length(improvements) BETWEEN 1 AND 2000),
    locale TEXT NOT NULL CHECK (locale IN ('es','en','pt-BR')),
    created_at BIGINT NOT NULL,
    UNIQUE(user_id, request_key)
);
CREATE INDEX demo_feedback_user_idx ON demo_feedback(user_id, created_at DESC);
CREATE TABLE demo_feedback_snooze (
    user_id UUID PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    until_at BIGINT NOT NULL
);
