CREATE INDEX cloud_jobs_invitation_activity
ON cloud_jobs(invitation_id, created_at DESC, id)
WHERE invitation_id IS NOT NULL;
