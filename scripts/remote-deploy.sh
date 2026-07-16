#!/usr/bin/env bash
set -euo pipefail

CONTEXT="${1:?build context required}"
VERSION="${2:?version required}"
PORT="${3:-9090}"
IMAGE="http-server-demo:${VERSION}"
ACTIVE="http-server-demo"
CANDIDATE="http-server-demo-candidate"
BACKUP="http-server-demo-backup"

[[ "${CONTEXT}" == /tmp/http-demo-* && -d "${CONTEXT}" ]] || { echo "Invalid context" >&2; exit 1; }
[[ "${VERSION}" =~ ^[0-9]{14}$ ]] || { echo "Invalid version" >&2; exit 1; }
[[ "${PORT}" =~ ^[0-9]+$ ]] || { echo "Invalid port" >&2; exit 1; }

docker build --pull --tag "${IMAGE}" "${CONTEXT}"

run_container() {
  local name="$1" host_port="$2" image="$3"
  docker run -d \
    --name "${name}" \
    --restart unless-stopped \
    --read-only \
    --tmpfs /tmp:rw,noexec,nosuid,size=16m \
    --cap-drop ALL \
    --security-opt no-new-privileges:true \
    --memory 128m \
    --cpus 0.50 \
    --pids-limit 64 \
    --ulimit nofile=1024:1024 \
    --publish "127.0.0.1:${host_port}:9090" \
    "${image}"
}

healthcheck() {
  local port="$1"
  for _ in $(seq 1 20); do
    if curl --fail --silent --max-time 2 "http://127.0.0.1:${port}/ping" | grep -q '"status":"ok"'; then
      return 0
    fi
    sleep 1
  done
  return 1
}

docker rm -f "${CANDIDATE}" >/dev/null 2>&1 || true
run_container "${CANDIDATE}" 19090 "${IMAGE}" >/dev/null
if ! healthcheck 19090; then
  docker logs "${CANDIDATE}" >&2 || true
  docker rm -f "${CANDIDATE}" >/dev/null 2>&1 || true
  echo "Candidate health check failed; active container was not changed." >&2
  exit 1
fi
docker rm -f "${CANDIDATE}" >/dev/null

docker rm -f "${BACKUP}" >/dev/null 2>&1 || true
if docker container inspect "${ACTIVE}" >/dev/null 2>&1; then
  docker stop "${ACTIVE}" >/dev/null
  docker rename "${ACTIVE}" "${BACKUP}"
fi

if run_container "${ACTIVE}" "${PORT}" "${IMAGE}" >/dev/null && healthcheck "${PORT}"; then
  docker rm -f "${BACKUP}" >/dev/null 2>&1 || true
  docker image prune -f --filter "until=168h" >/dev/null
  echo "Deployed ${IMAGE}."
  exit 0
fi

echo "New container failed after switch; restoring previous container." >&2
docker rm -f "${ACTIVE}" >/dev/null 2>&1 || true
if docker container inspect "${BACKUP}" >/dev/null 2>&1; then
  docker rename "${BACKUP}" "${ACTIVE}"
  docker start "${ACTIVE}" >/dev/null
fi
exit 1
