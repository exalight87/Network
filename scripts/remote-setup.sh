#!/usr/bin/env bash
set -euo pipefail

FQDN="${1:?fqdn required}"
SITE_ROOT="${2:?site root required}"
WEB_USER="${3:?web user required}"
BACKEND_PORT="${4:-9090}"

[[ "${FQDN}" =~ ^[a-z0-9.-]+$ ]] || { echo "Invalid fqdn" >&2; exit 1; }
[[ "${SITE_ROOT}" == /opt/sites/* ]] || { echo "Invalid site root" >&2; exit 1; }
[[ "${WEB_USER}" =~ ^[a-z_][a-z0-9_-]*$ ]] || { echo "Invalid web user" >&2; exit 1; }
[[ "${BACKEND_PORT}" =~ ^[0-9]+$ ]] || { echo "Invalid port" >&2; exit 1; }

if ! command -v docker >/dev/null 2>&1 || ! command -v curl >/dev/null 2>&1; then
  apt-get update
  DEBIAN_FRONTEND=noninteractive apt-get install -y docker.io curl
fi
systemctl enable --now docker
install -d -o "${WEB_USER}" -g "${WEB_USER}" -m 0755 /opt/http-server-demo

CADDY_FILE="/etc/caddy/sites.d/${FQDN}.caddy"
TMP_CADDY="$(mktemp)"
trap 'rm -f "${TMP_CADDY}"' EXIT
cat > "${TMP_CADDY}" <<EOF
${FQDN} {
    encode gzip zstd

    handle_path /api-demo/* {
        request_body {
            max_size 16KB
        }
        reverse_proxy 127.0.0.1:${BACKEND_PORT} {
            transport http {
                dial_timeout 2s
                response_header_timeout 6s
            }
        }
    }

    handle {
        root * ${SITE_ROOT}
        file_server
    }

    header {
        X-Content-Type-Options nosniff
        X-Frame-Options DENY
        Referrer-Policy strict-origin-when-cross-origin
        Permissions-Policy "camera=(), microphone=(), geolocation=()"
    }
}
EOF
install -o root -g root -m 0644 "${TMP_CADDY}" "${CADDY_FILE}"
caddy validate --config /etc/caddy/Caddyfile
systemctl reload caddy
echo "Docker and Caddy are ready for ${FQDN}."
