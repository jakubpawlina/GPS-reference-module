#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd -P)"
INSTALL_SCRIPT="$ROOT/tools/install-rpi-service.sh"
APP_USER="${GPS_APP_USER:-${SUDO_USER:-${USER:-pi}}}"
SUPPORTED_ENV=(
	GPS_SERIAL_PORT
	GPS_BAUD_RATE
	GPS_SERIAL_MAX_LINE_BYTES
	GPS_STATE_STALE_SECONDS
	GPS_DB_PATH
	GPS_MAX_DB_BYTES
	GPS_MAX_SSE_CONNECTIONS
	GPS_HTTP_HOST
	GPS_HTTP_PORT
	GPS_CORS_ORIGINS
	GPS_API_KEY
	GPS_CLOUD_WEBHOOK
)

if [[ ! -x "$INSTALL_SCRIPT" && ! -f "$INSTALL_SCRIPT" ]]; then
	echo "Missing install script: $INSTALL_SCRIPT" >&2
	exit 1
fi

if [[ "${1:-}" == "--uninstall" ]]; then
	exec sudo env GPS_APP_USER="$APP_USER" bash "$INSTALL_SCRIPT" --uninstall
fi

ENV_ARGS=("GPS_APP_USER=$APP_USER")
for name in "${SUPPORTED_ENV[@]}"; do
	if [[ -v "$name" ]]; then
		ENV_ARGS+=("$name=${!name}")
	fi
done

exec sudo env "${ENV_ARGS[@]}" bash "$INSTALL_SCRIPT"
