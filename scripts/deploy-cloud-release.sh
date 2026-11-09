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

# This release's v1.1 must be built from the saved engine, not a later experiment.
(cd "$release" && sha256sum -c web/shared/engine-v1.1-sources.sha256)
dockerfile=${PULSO_WEB_DOCKERFILE:-web/Dockerfile}
[[ "$dockerfile" == web/Dockerfile || "$dockerfile" == web/Dockerfile.invitation-release ]] || exit 2
docker build --build-arg "PULSO_SOURCE_REVISION=$revision" -f "$dockerfile" -t "$image" .

# Versioned engines live in persistent composition storage, not in the mutable
# app executable path. v1 is the exact binary currently deployed; v1.1 comes
# from saved_1.1. Never silently replace an already published engine version.
expected_v1=040c4fe0915be8eb3340f30e0aa3891093f1a317f0227142fc2ce0dcaa34ac32
docker exec -u 0 pulso-app-1 sh -eu -c '
  root="$PULSO_CLOUD_OUTPUT_DIR/engines"
  mkdir -p "$root/v1" "$root/v1.1"
  if [ ! -f "$root/v1/pulso_cloud_worker" ]; then
    test "$(sha256sum /app/pulso_cloud_worker | cut -d " " -f 1)" = "040c4fe0915be8eb3340f30e0aa3891093f1a317f0227142fc2ce0dcaa34ac32"
    cp /app/pulso_cloud_worker "$root/v1/pulso_cloud_worker"
  fi
  test "$(sha256sum "$root/v1/pulso_cloud_worker" | cut -d " " -f 1)" = "040c4fe0915be8eb3340f30e0aa3891093f1a317f0227142fc2ce0dcaa34ac32"
  chmod 755 "$root/v1/pulso_cloud_worker"
'
engine_container="pulso-engines-${revision:0:12}"
docker create --name "$engine_container" "$image" >/dev/null
docker cp "$engine_container:/app/pulso_cloud_worker" "$release/pulso_cloud_worker-v1.1"
docker rm "$engine_container" >/dev/null
engine_hash=$(sha256sum "$release/pulso_cloud_worker-v1.1" | cut -d ' ' -f 1)
docker exec -i -u 0 pulso-app-1 sh -eu -c 'cat > /tmp/pulso-engine-candidate' < "$release/pulso_cloud_worker-v1.1"
docker exec -u 0 -e EXPECTED_ENGINE_HASH="$engine_hash" pulso-app-1 sh -eu -c '
  target="$PULSO_CLOUD_OUTPUT_DIR/engines/v1.1/pulso_cloud_worker"
  if [ -f "$target" ]; then
    test "$(sha256sum "$target" | cut -d " " -f 1)" = "$EXPECTED_ENGINE_HASH"
  else
    cp /tmp/pulso-engine-candidate "$target"
  fi
  chmod 755 "$target"
  chown -R 10001:10001 "$PULSO_CLOUD_OUTPUT_DIR/engines"
  test -x "$target"
'
docker exec -u 0 -e RELEASE_REVISION="$revision" pulso-app-1 sh -eu -c '
  root="$PULSO_CLOUD_OUTPUT_DIR/engines"
  if [ ! -f "$root/v1/source-revision" ]; then printf "%s\n" ef945acd4daf2d3d61b84c732a5d3ab8e810588b > "$root/v1/source-revision"; fi
  if [ ! -f "$root/v1.1/source-revision" ]; then printf "%s\n" 80f896ce75b0c905258d50b29cb18caa1ec7c717 > "$root/v1.1/source-revision"; fi
'
printf 'ENGINE v1 %s\nENGINE v1.1 %s\n' "$expected_v1" "$engine_hash"

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
