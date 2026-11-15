#!/usr/bin/env bash
set -Eeuo pipefail
umask 077
release=$(realpath "${1:?release directory required}")
revision=${2:?source revision required}
[[ "$revision" =~ ^[0-9a-f]{40}$ ]] || exit 2
[[ "$release" == "/www/Pulso-release-${revision:0:12}" ]] || exit 2
[[ -f "$release/web/Dockerfile.audio-release" ]] || exit 2
# Share the engine-release lock so two releases cannot race the static symlink.
exec 9>/www/pulso-engine-release.lock
flock -n 9 || { echo 'Another deployment is active'; exit 3; }
cd "$release"
image="pulso-web-audio:$revision"
docker build --build-arg "PULSO_SOURCE_REVISION=$revision" -f web/Dockerfile.audio-release -t "$image" .
static="/www/Pulso-web-static-${revision:0:12}"
mkdir "$static"
temporary="pulso-audio-static-${revision:0:12}"
docker create --name "$temporary" "$image" /bin/true >/dev/null
docker cp "$temporary:/source/frontend/dist/." "$static/"
docker rm "$temporary" >/dev/null
for note in A0 C1 C2 C3 C4 C5 C6 C7 C8 Fs1 Fs2 Fs3 Fs4 Fs5 Fs6 Fs7; do
  test -s "$static/piano/INSPiano_Vel03_${note}_01.mp3"
  test -s "$static/piano/INSPiano_Vel03_${note}_01.ogg"
done
previous=$(readlink -f /www/Pulso-web-current)
if [[ -d "$previous/assets" ]]; then cp -an "$previous/assets/." "$static/assets/"; fi
chmod -R a+rX "$static"
nginx -t
activated=0
rollback() {
  code=$?
  trap - ERR
  if [[ "$activated" == 1 ]]; then
    ln -s "$previous" "/www/Pulso-audio-rollback-${revision:0:12}" &&
      mv -Tf "/www/Pulso-audio-rollback-${revision:0:12}" /www/Pulso-web-current || true
    systemctl reload nginx || true
    echo 'PLAYER_DEPLOY_FAILED: previous static site restored'
  fi
  exit "$code"
}
trap rollback ERR
ln -s "$static" "/www/Pulso-audio-next-${revision:0:12}"
activated=1
mv -Tf "/www/Pulso-audio-next-${revision:0:12}" /www/Pulso-web-current
systemctl reload nginx
curl --fail --silent https://pulso-audio.com/api/health >/dev/null
curl --fail --silent https://pulso-audio.com/cloud >/dev/null
piano_headers=$(curl --fail --silent --head https://pulso-audio.com/piano/INSPiano_Vel03_C4_01.mp3)
[[ "$piano_headers" == *audio/mpeg* ]]
printf '%s\n' "$revision" > /www/Pulso/UI_REVISION
activated=0
trap - ERR
printf 'DEPLOYED_WEB_AUDIO %s (API and musical engine unchanged)\n' "$revision"
