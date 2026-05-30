#!/usr/bin/env bash
set -Eeuo pipefail

ROOT="$(cd -- "$(dirname -- "${BASH_SOURCE[0]}")/.." >/dev/null 2>&1 && pwd -P)"
INSTALL_SCRIPT="$ROOT/tools/install-rpi-service.sh"
APP_USER="${GPS_APP_USER:-${SUDO_USER:-${USER:-pi}}}"

if [[ ! -x "$INSTALL_SCRIPT" && ! -f "$INSTALL_SCRIPT" ]]; then
  echo "Missing install script: $INSTALL_SCRIPT" >&2
  exit 1
fi

if [[ "${1:-}" == "--uninstall" ]]; then
  exec sudo env GPS_APP_USER="$APP_USER" bash "$INSTALL_SCRIPT" --uninstall
fi

exec sudo env GPS_APP_USER="$APP_USER" bash "$INSTALL_SCRIPT"
