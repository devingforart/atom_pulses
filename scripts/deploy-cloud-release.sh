#!/usr/bin/env bash
# Run on the production host against a fresh git-archive release directory.
# Does not touch .env, the database or persistent composition storage.
set -Eeuo pipefail
release=$(realpath "${1:?usage: deploy-cloud-release.sh /www/Pulso-release-REV REV}")
revision=${2:?source revision required}
[[ "$revision" =~ ^[0-9a-f]{40}$ ]] || { echo 'Invalid revision'; exit 2; }
[[ "$release" == "/www/Pulso-release-${revision:0:12}" ]] || {
  echo 'Release path must match the exact revision under /www'; exit 2;
}
[[ -f "$release/web/Dockerfile" && -f /www/Pulso/.env ]] || exit 2
cd "$release"
image="pulso-web:$revision"
docker build --build-arg "PULSO_SOURCE_REVISION=$revision" -f web/Dockerfile -t "$image" .

# Building can take time: check again immediately before replacing the API.
active=$(docker exec pulso-postgres-1 psql -U pulso -d pulso -Atc \
  "select count(*) from cloud_jobs where status in ('queued','running');")
[[ "$active" == 0 ]] || { echo 'Cloud has active jobs; image built, activation deferred.'; exit 3; }
previous=$(docker inspect pulso-app-1 --format '{{.Image}}')
docker tag "$previous" "pulso-web:rollback-${revision:0:12}"
static="/www/Pulso-web-static-${revision:0:12}"
mkdir "$static"
temporary="pulso-static-${revision:0:12}"
docker create --name "$temporary" "$image" >/dev/null
docker cp "$temporary:/app/frontend/." "$static/"
docker rm "$temporary" >/dev/null
if [[ -d /www/Pulso-web-current/assets ]]; then
  cp -an /www/Pulso-web-current/assets/. "$static/assets/"
fi
chmod -R a+rX "$static"
nginx -t

compose=(docker compose --project-directory /www/Pulso -f /www/Pulso/docker-compose.production.yml)
docker tag "$image" pulso-web:local
"${compose[@]}" up -d --no-build --no-deps --force-recreate app
healthy=0
for attempt in {1..30}; do
  if curl --fail --silent http://127.0.0.1:8188/api/health >/dev/null; then
    healthy=1
    break
  fi
  sleep 2
done
if [[ "$healthy" != 1 ]]; then
  docker tag "$previous" pulso-web:local
  "${compose[@]}" up -d --no-build --no-deps --force-recreate app
  echo 'Health check failed; previous API image restored.'
  exit 1
fi
ln -s "$static" "/www/Pulso-web-next-${revision:0:12}"
mv -Tf "/www/Pulso-web-next-${revision:0:12}" /www/Pulso-web-current
systemctl reload nginx
curl --fail --silent https://pulso.cargo-core.com/api/health >/dev/null
printf 'DEPLOYED %s\n' "$revision"
docker inspect pulso-app-1 --format '{{index .Config.Labels "org.opencontainers.image.revision"}}'
