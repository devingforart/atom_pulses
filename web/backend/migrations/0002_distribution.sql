ALTER TABLE users ADD COLUMN email_verified BOOLEAN NOT NULL DEFAULT FALSE;

CREATE TABLE auth_tokens (
    token_hash TEXT PRIMARY KEY,
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    purpose TEXT NOT NULL CHECK (purpose IN ('verify_email', 'reset_password')),
    expires_at BIGINT NOT NULL,
    used_at BIGINT,
    created_at BIGINT NOT NULL
);
CREATE INDEX auth_tokens_user_idx ON auth_tokens(user_id, purpose);
CREATE INDEX auth_tokens_expiry_idx ON auth_tokens(expires_at);

CREATE TABLE device_codes (
    code_hash TEXT PRIMARY KEY,
    secret_hash TEXT NOT NULL,
    device_id TEXT NOT NULL,
    device_name TEXT NOT NULL,
    user_id UUID REFERENCES users(id) ON DELETE CASCADE,
    approved_at BIGINT,
    expires_at BIGINT NOT NULL,
    consumed_at BIGINT,
    created_at BIGINT NOT NULL
);
CREATE INDEX device_codes_expiry_idx ON device_codes(expires_at);

CREATE TABLE activations (
    id UUID PRIMARY KEY,
    user_id UUID NOT NULL REFERENCES users(id) ON DELETE CASCADE,
    device_id TEXT NOT NULL,
    device_name TEXT NOT NULL,
    refresh_token_hash TEXT NOT NULL UNIQUE,
    last_seen_at BIGINT NOT NULL,
    created_at BIGINT NOT NULL,
    revoked_at BIGINT,
    UNIQUE(user_id, device_id)
);
CREATE INDEX activations_user_idx ON activations(user_id, revoked_at);

CREATE TABLE entitlements (
    user_id UUID PRIMARY KEY REFERENCES users(id) ON DELETE CASCADE,
    product TEXT NOT NULL DEFAULT 'studio',
    status TEXT NOT NULL DEFAULT 'active',
    updates_until BIGINT,
    source TEXT NOT NULL DEFAULT 'stripe',
    created_at BIGINT NOT NULL,
    updated_at BIGINT NOT NULL
);
