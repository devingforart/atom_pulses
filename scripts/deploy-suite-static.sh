#!/usr/bin/env bash
# UI-only deployment. Leaves API, worker, credentials, database and jobs untouched.
set -Eeuo pipefail
revision=${1:?source commit required}
[[ "$revision" =~ ^[0-9a-f]{40}$ ]] || exit 2
archive="/www/Pulso-suite-ui-$revision.tar.gz"
target="/www/Pulso-web-ui-$revision"
[[ -f "$archive" && -L /www/Pulso-web-current ]] || exit 2
previous=$(readlink -f /www/Pulso-web-current)
[[ "$previous" == /www/Pulso-web-* && -f "$previous/index.html" ]] || exit 2
[[ ! -e "$target" && ! -e "/www/Pulso-web-next-ui-$revision" ]] || exit 2
nginx -t
mkdir "$target"
tar -xzf "$archive" -C "$target"
[[ -f "$target/index.html" && -f "$target/suite-release.json" && -d "$target/assets" ]] || exit 2
# Retain hashed assets for browsers with the previous entrypoint still open.
cp -an "$previous/assets/." "$target/assets/"
chmod -R a+rX "$target"
printf 'ROLLBACK_TARGET=%s\n' "$previous"
ln -s "$target" "/www/Pulso-web-next-ui-$revision"
mv -Tf "/www/Pulso-web-next-ui-$revision" /www/Pulso-web-current
if ! curl --fail --silent https://pulso.cargo-core.com/api/health >/dev/null; then
  ln -s "$previous" "/www/Pulso-web-rollback-ui-$revision"
  mv -Tf "/www/Pulso-web-rollback-ui-$revision" /www/Pulso-web-current
  echo 'Health check failed; previous UI restored.'
  exit 1
fi
printf 'UI_DEPLOYED=%s\n' "$revision"
echo 'API_AND_WORKER_UNCHANGED'
