#!/usr/bin/env bash
# Run against an immutable git archive. No credential/configuration replacement.
set -Eeuo pipefail
umask 077
release=$(realpath "${1:?release directory required}")
revision=${2:?release source revision required}
[[ "$revision" =~ ^[0-9a-f]{40}$ ]] || exit 2
[[ "$release" == "/www/Pulso-release-${revision:0:12}" ]] || exit 2
[[ -f "$release/web/Dockerfile.engine-release" && -f /www/Pulso/.env ]] || exit 2
exec 9>/www/pulso-engine-release.lock
flock -n 9 || { echo 'Another engine deployment is active'; exit 3; }
cd "$release"
sha256sum -c web/shared/engine-0.1.3-sources.sha256
image="pulso-web:$revision"
dockerfile=${PULSO_ENGINE_RELEASE_DOCKERFILE:-web/Dockerfile.engine-release}
migration_version=${PULSO_ENGINE_RELEASE_MIGRATION:-18}
public_label=${PULSO_ENGINE_PUBLIC_LABEL:-0.1.3}
[[ "$dockerfile" == web/Dockerfile.engine-release || "$dockerfile" == web/Dockerfile.engine-label-release ]] || exit 2
[[ "$migration_version" == 18 || "$migration_version" == 19 ]] || exit 2
[[ "$public_label" == 0.1.3 || "$public_label" == 1.2 ]] || exit 2
docker build --build-arg "PULSO_SOURCE_REVISION=$revision" -f "$dockerfile" -t "$image" .

# Store the Linux worker under a NEW version. Previously published workers are
# immutable, live in persistent storage and are not rebuilt/replaced here.
docker exec -u 0 pulso-app-1 sh -eu -c '
  root="$PULSO_CLOUD_OUTPUT_DIR/engines"
  test -x "$root/v1/pulso_cloud_worker"
  test -x "$root/v1.1/pulso_cloud_worker"
  sha256sum "$root/v1/pulso_cloud_worker" "$root/v1.1/pulso_cloud_worker"
' > "$release/previous-engine-hashes.sha256"
temporary="pulso-engine-${revision:0:12}"
docker create --name "$temporary" "$image" >/dev/null
docker cp "$temporary:/app/pulso_cloud_worker" "$release/pulso_cloud_worker-0.1.3"
docker rm "$temporary" >/dev/null
engine_hash=$(sha256sum "$release/pulso_cloud_worker-0.1.3" | cut -d ' ' -f 1)
docker exec -i -u 0 pulso-app-1 sh -eu -c 'cat > /tmp/pulso-engine-0.1.3-candidate' < "$release/pulso_cloud_worker-0.1.3"
docker exec -u 0 -e EXPECTED_ENGINE_HASH="$engine_hash" pulso-app-1 sh -eu -c '
  root="$PULSO_CLOUD_OUTPUT_DIR/engines/0.1.3"
  mkdir -p "$root"
  if [ -f "$root/pulso_cloud_worker" ]; then
    test "$(sha256sum "$root/pulso_cloud_worker" | cut -d " " -f 1)" = "$EXPECTED_ENGINE_HASH"
  else
    cp /tmp/pulso-engine-0.1.3-candidate "$root/pulso_cloud_worker"
  fi
  chmod 755 "$root/pulso_cloud_worker"
  printf "%s\n" 83a3faaee73c8044d05680adaa7aef81abaab10b > "$root/source-revision"
  printf "%s\n" "$EXPECTED_ENGINE_HASH" > "$root/worker.sha256"
  chown -R 10001:10001 "$root"
'
active=$(docker exec pulso-postgres-1 psql -U pulso -d pulso -Atc "select count(*) from cloud_jobs where status in ('queued','running');")
[[ "$active" == 0 ]] || { echo 'IMAGE_READY_ACTIVATION_DEFERRED: active Cloud jobs; none cancelled'; exit 3; }
backup="/www/Pulso-engine-backups/${revision:0:12}"
mkdir -p "$backup"
docker exec pulso-postgres-1 pg_dump -U pulso -d pulso | gzip > "$backup/pulso.sql.gz"
previous=$(docker inspect pulso-app-1 --format '{{.Image}}')
previous_static=$(readlink -f /www/Pulso-web-current)
docker tag "$previous" "pulso-web:rollback-${revision:0:12}"

static="/www/Pulso-web-static-${revision:0:12}"
mkdir "$static"
temporary="pulso-static-${revision:0:12}"
docker create --name "$temporary" "$image" >/dev/null
docker cp "$temporary:/app/frontend/." "$static/"
docker rm "$temporary" >/dev/null
if [[ -d /www/Pulso-web-current/assets ]]; then cp -an /www/Pulso-web-current/assets/. "$static/assets/"; fi
chmod -R a+rX "$static"
nginx -t
compose=(docker compose --project-directory /www/Pulso -f /www/Pulso/docker-compose.production.yml)
activated=0
rollback() {
  code=$?
  trap - ERR
  if [[ "$activated" == 1 ]]; then
    docker tag "$previous" pulso-web:local || true
    "${compose[@]}" up -d --no-build --no-deps --force-recreate app || true
    ln -s "$previous_static" "/www/Pulso-web-rollback-${revision:0:12}" &&
      mv -Tf "/www/Pulso-web-rollback-${revision:0:12}" /www/Pulso-web-current || true
    systemctl reload nginx || true
    echo 'ACTIVATION_FAILED: previous API and static site restored; expanded engine enum is backward compatible'
  fi
  exit "$code"
}
trap rollback ERR
docker tag "$image" pulso-web:local
activated=1
"${compose[@]}" up -d --no-build --no-deps --force-recreate app
healthy=0
for attempt in {1..30}; do
  if curl --fail --silent http://127.0.0.1:8188/api/health >/dev/null; then healthy=1; break; fi
  sleep 2
done
if [[ "$healthy" != 1 ]]; then
  echo 'HEALTH_FAILED'; false
fi
migration=$(docker exec pulso-postgres-1 psql -U pulso -d pulso -Atc "select success from _sqlx_migrations where version=$migration_version;")
[[ "$migration" == t ]] || { echo "Migration $migration_version is not applied; static activation stopped"; false; }
docker exec -i pulso-app-1 sh -eu -c 'sha256sum -c -' < "$release/previous-engine-hashes.sha256"
docker exec -e EXPECTED_ENGINE_HASH="$engine_hash" pulso-app-1 sh -eu -c '
  test "$(sha256sum "$PULSO_CLOUD_OUTPUT_DIR/engines/0.1.3/pulso_cloud_worker" | cut -d " " -f 1)" = "$EXPECTED_ENGINE_HASH"
  test -x "$PULSO_CLOUD_OUTPUT_DIR/engines/0.1.3/pulso_cloud_worker"
'
ln -s "$static" "/www/Pulso-web-next-${revision:0:12}"
mv -Tf "/www/Pulso-web-next-${revision:0:12}" /www/Pulso-web-current
systemctl reload nginx
curl --fail --silent https://pulso.cargo-core.com/api/health >/dev/null
activated=0
trap - ERR
printf 'DEPLOYED engine=%s source=%s release=%s linux_sha256=%s\n' "$public_label" 83a3faaee73c8044d05680adaa7aef81abaab10b "$revision" "$engine_hash"
docker inspect pulso-app-1 --format '{{index .Config.Labels "org.opencontainers.image.revision"}}'
