CREATE TABLE cloud_invitation_emails (
    invitation_id UUID PRIMARY KEY REFERENCES cloud_invitations(id) ON DELETE CASCADE,
    status TEXT NOT NULL CHECK(status IN ('sending','accepted','failed','unknown')),
    first_attempt_at BIGINT NOT NULL,
    updated_at BIGINT NOT NULL,
    payload_hash TEXT NOT NULL,
    provider_id TEXT
);
