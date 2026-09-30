-- Existing Cloud jobs retain the historical renderer; new requests may explicitly
-- select the AI-sovereign renderer used by the local composition studio.
ALTER TABLE cloud_jobs ADD COLUMN ai_sovereign BOOLEAN NOT NULL DEFAULT FALSE;
