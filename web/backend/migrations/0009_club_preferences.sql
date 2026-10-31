ALTER TABLE cloud_jobs
    ADD COLUMN club_layer_pace TEXT NOT NULL DEFAULT 'auto'
        CHECK (club_layer_pace IN ('auto', '8-16', '16-32', '32-64')),
    ADD COLUMN club_variety BOOLEAN NOT NULL DEFAULT false;
