#!/usr/bin/env bash
# Admin-only update: preserve private configuration and published music engines.
set -Eeuo pipefail
umask 077
cloud_revision=${1:?Cloud revision required}
growth_revision=${2:?Growth revision required}
[[ "$cloud_revision" =~ ^[0-9a-f]{40}$ && "$growth_revision" =~ ^[0-9a-f]{40}$ ]] || exit 2
cloud="/www/Pulso-release-${cloud_revision:0:12}"
growth="/www/Pulso-growth-release-${growth_revision:0:12}"
[[ -f "$cloud/web/backend/src/invitation_activity.rs" && -f "$growth/frontend/src/InvitationActivity.tsx" ]] || exit 2
exec 9>/www/pulso-invitation-activity-deploy.lock
flock -n 9 || { echo 'Another activity deployment is running'; exit 3; }
previous_growth=$(docker image inspect deploy-growth:latest --format '{{.Id}}')
docker tag "$previous_growth" "deploy-growth:rollback-${growth_revision:0:12}"
docker tag "$previous_growth" deploy-growth:before-invitations
docker build --build-arg "PULSO_SOURCE_REVISION=$growth_revision" -f "$growth/Dockerfile.invitations" -t "deploy-growth:$growth_revision" "$growth"
backup="/www/Pulso-invitation-backups/activity-$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$backup"
docker exec pulso-postgres-1 pg_dump -U pulso -d pulso | gzip > "$backup/pulso.sql.gz"
python3 - "$backup" <<'PY'
import sqlite3,sys
source=sqlite3.connect('file:/www/Pulso-growth-data/growth.sqlite3?mode=ro',uri=True)
target=sqlite3.connect(sys.argv[1]+'/growth.sqlite3')
source.backup(target)
assert target.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
target.close();source.close()
PY
PULSO_WEB_DOCKERFILE=web/Dockerfile.invitation-release bash "$cloud/scripts/deploy-cloud-release.sh" "$cloud" "$cloud_revision"
python3 - <<'PY'
import sqlite3
c=sqlite3.connect('file:/www/Pulso-growth-data/growth.sqlite3?mode=ro',uri=True)
assert c.execute("SELECT count(*) FROM jobs WHERE state='running'").fetchone()[0]==0, 'Growth jobs active; activation deferred'
assert c.execute("SELECT count(*) FROM posts WHERE status='publishing' AND NOT (platform='tiktok' AND remote_id IS NOT NULL)").fetchone()[0]==0, 'Growth delivery active; activation deferred'
PY
docker tag "deploy-growth:$growth_revision" deploy-growth:latest
compose=(docker compose --project-directory /www/Pulso-growth/deploy -f /www/Pulso-growth/deploy/docker-compose.production.yml)
"${compose[@]}" up -d --no-build --no-deps --force-recreate growth
healthy=0
for attempt in {1..30}; do
  if curl --fail --silent http://127.0.0.1:8790/api/health >/dev/null; then healthy=1; break; fi
  sleep 2
done
if [[ "$healthy" != 1 ]]; then
  docker tag "$previous_growth" deploy-growth:latest
  "${compose[@]}" up -d --no-build --no-deps --force-recreate growth
  echo 'Growth health failed; previous image restored'; exit 1
fi
python3 "$cloud/scripts/smoke-invitation-activity.py"
tar -cf - -C "$growth" backend frontend branding marketing scripts docs Dockerfile Dockerfile.invitations README.md | tar -xf - -C /www/Pulso-growth
printf '%s\n' "$growth_revision" > /www/Pulso-growth/REVISION
printf '%s\n' "$growth_revision" > /www/Pulso-growth/UI_REVISION
echo "DEPLOYED_AND_VERIFIED Cloud=$cloud_revision Growth=$growth_revision"
