#!/usr/bin/env bash
# Run only on the existing PULSO host. No secret is passed through arguments.
set -Eeuo pipefail
umask 077
cloud_revision=${1:?Cloud revision required}
growth_revision=${2:?Growth revision required}
[[ "$cloud_revision" =~ ^[0-9a-f]{40}$ && "$growth_revision" =~ ^[0-9a-f]{40}$ ]] || exit 2
cloud="/www/Pulso-release-${cloud_revision:0:12}"
growth="/www/Pulso-growth-release-${growth_revision:0:12}"
[[ -f "$cloud/web/Dockerfile.invitation-release" && -f "$growth/Dockerfile.invitations" ]] || exit 2
previous_growth=$(docker inspect deploy-growth-1 --format '{{.Image}}')
docker tag "$previous_growth" deploy-growth:before-invitations
docker build --build-arg "PULSO_SOURCE_REVISION=$growth_revision" -f "$growth/Dockerfile.invitations" -t "deploy-growth:$growth_revision" "$growth"

# Back up configuration and databases before applying the API migrations.
backup="/www/Pulso-invitation-backups/$(date -u +%Y%m%dT%H%M%SZ)"
mkdir -p "$backup"
export INVITATION_BACKUP="$backup"
docker exec pulso-postgres-1 pg_dump -U pulso -d pulso | gzip > "$backup/pulso.sql.gz"
python3 - <<'PY'
import json, os, secrets, shutil, sqlite3, subprocess
from pathlib import Path
backup=Path(os.environ['INVITATION_BACKUP'])
source=sqlite3.connect('file:/www/Pulso-growth-data/growth.sqlite3?mode=ro',uri=True)
target=sqlite3.connect(backup/'growth.sqlite3')
source.backup(target)
assert target.execute('PRAGMA integrity_check').fetchone()[0]=='ok'
target.close()
# Existing vault credentials override environment; never silently connect to a
# different Cloud if a previous administrator has configured the bridge.
assert not source.execute("SELECT name FROM service_credentials WHERE name IN ('CLOUD_API_URL','CLOUD_ADMIN_TOKEN')").fetchall(), 'Existing Growth bridge configuration needs review'
source.close()
container=json.loads(subprocess.check_output(['docker','inspect','pulso-app-1']))[0]
env=dict(item.split('=',1) for item in container['Config']['Env'])
token=env.get('PULSO_GROWTH_ADMIN_TOKEN') or secrets.token_urlsafe(48)
assert len(token)>=32
def update(path,changes):
    path=Path(path)
    shutil.copyfile(path,backup/(path.parent.name+'.env'))
    lines=path.read_text().splitlines()
    for key,value in changes.items():
        lines=[line for line in lines if not line.startswith(key+'=')]
        lines.append(key+'='+value)
    temporary=path.with_name('.env.invitation-next')
    temporary.write_text('\n'.join(lines)+'\n')
    os.chmod(temporary,0o600)
    os.replace(temporary,path)
update('/www/Pulso/.env',{'PULSO_GROWTH_ADMIN_TOKEN':token})
update('/www/Pulso-growth/.env',{'GROWTH_CLOUD_ADMIN_TOKEN':token,'GROWTH_CLOUD_API_URL':'https://pulso-audio.com'})
print('Private bridge configured; secrets were not logged')
PY
PULSO_WEB_DOCKERFILE=web/Dockerfile.invitation-release bash "$cloud/scripts/deploy-cloud-release.sh" "$cloud" "$cloud_revision"

# Do not interrupt a social delivery already in flight.
python3 - <<'PY'
import sqlite3
c=sqlite3.connect('file:/www/Pulso-growth-data/growth.sqlite3?mode=ro',uri=True)
assert c.execute("SELECT count(*) FROM jobs WHERE state='running'").fetchone()[0]==0, 'Growth has active jobs; activation deferred'
assert c.execute("SELECT count(*) FROM posts WHERE status='publishing' AND NOT (platform='tiktok' AND remote_id IS NOT NULL)").fetchone()[0]==0, 'Growth has an active direct delivery; activation deferred'
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
  echo 'Growth health failed; restored previous image'
  exit 1
fi
# Keep server sources current without overwriting private env or operational
# compose/nginx settings. The image, not this directory, serves the application.
tar -cf - -C "$growth" backend frontend branding marketing scripts docs Dockerfile Dockerfile.invitations README.md | tar -xf - -C /www/Pulso-growth
printf '%s\n' "$growth_revision" > /www/Pulso-growth/REVISION
python3 "$cloud/scripts/smoke-growth-invitations.py"
echo "DEPLOYED_AND_VERIFIED Cloud=$cloud_revision Growth=$growth_revision"
