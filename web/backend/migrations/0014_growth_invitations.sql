CREATE TABLE cloud_invitations (
    id UUID PRIMARY KEY,
    code_hash TEXT NOT NULL UNIQUE,
    code_hint TEXT NOT NULL,
    label TEXT NOT NULL CHECK(length(label) BETWEEN 1 AND 120),
    target_email TEXT,
    credits BIGINT NOT NULL CHECK(credits BETWEEN 3 AND 1000000),
    daily_limit BIGINT NOT NULL CHECK(daily_limit BETWEEN 1 AND 1000),
    weekly_limit BIGINT NOT NULL CHECK(weekly_limit BETWEEN 1 AND 10000),
    monthly_limit BIGINT NOT NULL CHECK(monthly_limit BETWEEN 1 AND 100000),
    max_redemptions BIGINT NOT NULL CHECK(max_redemptions BETWEEN 1 AND 10000),
    expires_at BIGINT,
    revoked_at BIGINT,
    created_at BIGINT NOT NULL
);
CREATE TABLE cloud_invitation_redemptions (
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    invitation_id UUID NOT NULL REFERENCES cloud_invitations(id),
    redeemed_at BIGINT NOT NULL,
    PRIMARY KEY(user_id,invitation_id)
);
CREATE INDEX cloud_invitation_redemptions_campaign ON cloud_invitation_redemptions(invitation_id);
