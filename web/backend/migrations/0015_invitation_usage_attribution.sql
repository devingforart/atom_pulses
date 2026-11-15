ALTER TABLE cloud_jobs ADD COLUMN invitation_id UUID REFERENCES cloud_invitations(id);
CREATE INDEX cloud_jobs_invitation_usage ON cloud_jobs(user_id,invitation_id) WHERE status='completed';
