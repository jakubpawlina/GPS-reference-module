#!/usr/bin/env bash
# GPS Reference Module - Raspberry Pi install script
# Usage: sudo bash tools/install-rpi-service.sh [--uninstall]
set -euo pipefail

# ── Colours ────────────────────────────────────────────────────────────────────
RED='\033[0;31m'
GREEN='\033[0;32m'
YELLOW='\033[1;33m'
CYAN='\033[0;36m'
NC='\033[0m'
info() { echo -e "${GREEN}[INFO]${NC}  $*"; }
warn() { echo -e "${YELLOW}[WARN]${NC}  $*"; }
step() { echo -e "${CYAN}[....] $*${NC}"; }
die() {
	echo -e "${RED}[FAIL]${NC}  $*" >&2
	exit 1
}

# ── Configuration (override via environment) ───────────────────────────────────
APP_USER="${GPS_APP_USER:-refmod}"
INSTALL_DIR="/opt/gps-reference"
DATA_DIR="/var/lib/gps-reference"
SERVICE="gps-reference"
SERIAL_PORT="${GPS_SERIAL_PORT:-/dev/ttyUSB0}"
HTTP_PORT="${GPS_HTTP_PORT:-8000}"
MAX_DB_BYTES="${GPS_MAX_DB_BYTES:-4294967296}" # 4 GB
CLOUD_WEBHOOK="${GPS_CLOUD_WEBHOOK:-}"

ROOT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
SERVICE_DIR="$ROOT_DIR/service"
REQUIRED_FILES=(main.py api.py database.py reader.py config.py requirements.txt gps-reference.service)

# ── Guard: must run as root ────────────────────────────────────────────────────
[[ $EUID -eq 0 ]] || die "Run with sudo: sudo bash tools/install-rpi-service.sh"

# ── Uninstall path ─────────────────────────────────────────────────────────────
if [[ "${1:-}" == "--uninstall" ]]; then
	step "Stopping and disabling service…"
	systemctl stop "$SERVICE" 2>/dev/null || true
	systemctl disable "$SERVICE" 2>/dev/null || true
	rm -f "/etc/systemd/system/${SERVICE}.service"
	rm -rf "/etc/systemd/system/${SERVICE}.service.d"
	systemctl daemon-reload
	step "Removing application files…"
	rm -rf "$INSTALL_DIR"
	info "Uninstalled.  Data in $DATA_DIR was kept - remove manually if desired."
	exit 0
fi

echo ""
echo -e "${CYAN}━━━  GPS Reference Module - Setup  ━━━${NC}"
echo ""

# ── Pre-flight checks ──────────────────────────────────────────────────────────
step "Checking source files…"
for f in "${REQUIRED_FILES[@]}"; do
	[[ -f "$SERVICE_DIR/$f" ]] || die "Missing source file: $SERVICE_DIR/$f"
done
info "All source files present"

step "Checking user '$APP_USER'…"
id "$APP_USER" &>/dev/null || die "User '$APP_USER' does not exist. Set GPS_APP_USER=<your-user>"

step "Checking Python version…"
PYTHON="$(command -v python3)" || die "python3 not found"
PY_VER="$("$PYTHON" -c 'import sys; print(f"{sys.version_info.major}.{sys.version_info.minor}")')"
info "Python $PY_VER"
"$PYTHON" -c 'import sys; sys.exit(0 if sys.version_info >= (3, 9) else 1)' ||
	die "Python 3.9+ required (found $PY_VER)"

# ── System packages ────────────────────────────────────────────────────────────
step "Installing system packages…"
apt-get update -qq
apt-get install -y -qq python3-pip python3-venv python3-dev curl
info "System packages ready"

# ── Directories ────────────────────────────────────────────────────────────────
step "Creating directories…"
mkdir -p "$INSTALL_DIR" "$DATA_DIR"
chown "$APP_USER:$APP_USER" "$DATA_DIR"
info "$INSTALL_DIR  (application)"
info "$DATA_DIR  (database, owned by $APP_USER)"

# ── Copy application files ─────────────────────────────────────────────────────
step "Copying application files…"
for f in main.py api.py database.py reader.py config.py requirements.txt; do
	cp "$SERVICE_DIR/$f" "$INSTALL_DIR/$f"
	info "  → $f"
done
chown -R root:root "$INSTALL_DIR"
chmod 644 "$INSTALL_DIR"/*.py "$INSTALL_DIR/requirements.txt"

# ── Virtual environment ────────────────────────────────────────────────────────
step "Setting up virtual environment…"
if [[ ! -d "$INSTALL_DIR/venv" ]]; then
	"$PYTHON" -m venv "$INSTALL_DIR/venv"
	info "Virtual environment created"
else
	info "Virtual environment already exists - reusing"
fi

step "Installing Python dependencies…"
"$INSTALL_DIR/venv/bin/pip" install --quiet --upgrade pip
"$INSTALL_DIR/venv/bin/pip" install --quiet -r "$INSTALL_DIR/requirements.txt"
info "Dependencies installed"

# ── Import smoke test ──────────────────────────────────────────────────────────
step "Running import smoke test…"
"$INSTALL_DIR/venv/bin/python" - <<'PYEOF'
import fastapi, uvicorn, aiosqlite, serial, httpx
print("  fastapi", fastapi.__version__)
print("  uvicorn", uvicorn.__version__)
print("  aiosqlite", aiosqlite.__version__)
print("  httpx", httpx.__version__)
PYEOF
info "All imports OK"

# ── dialout group ─────────────────────────────────────────────────────────────
step "Checking serial port group membership…"
if ! id -nG "$APP_USER" | grep -qw dialout; then
	usermod -aG dialout "$APP_USER"
	warn "Added '$APP_USER' to group 'dialout' - a re-login (or reboot) is required for this to take effect"
else
	info "'$APP_USER' is already in group 'dialout'"
fi

# ── Systemd service ────────────────────────────────────────────────────────────
step "Installing systemd service…"
cp "$SERVICE_DIR/gps-reference.service" "/etc/systemd/system/${SERVICE}.service"

OVERRIDE_DIR="/etc/systemd/system/${SERVICE}.service.d"
mkdir -p "$OVERRIDE_DIR"
cat >"$OVERRIDE_DIR/local.conf" <<EOF
[Service]
User=${APP_USER}
Group=${APP_USER}
Environment=GPS_SERIAL_PORT=${SERIAL_PORT}
Environment=GPS_DB_PATH=${DATA_DIR}/data.db
Environment=GPS_HTTP_PORT=${HTTP_PORT}
Environment=GPS_MAX_DB_BYTES=${MAX_DB_BYTES}
# Uncomment to override defaults:
# Environment=GPS_BAUD_RATE=115200
# Environment=GPS_HTTP_HOST=0.0.0.0
EOF

if [[ -n "$CLOUD_WEBHOOK" ]]; then
	echo "Environment=GPS_CLOUD_WEBHOOK=${CLOUD_WEBHOOK}" >>"$OVERRIDE_DIR/local.conf"
	info "Cloud webhook configured: $CLOUD_WEBHOOK"
else
	echo "# Environment=GPS_CLOUD_WEBHOOK=https://your-endpoint/ingest" >>"$OVERRIDE_DIR/local.conf"
	info "Cloud webhook not set (edit $OVERRIDE_DIR/local.conf to add one)"
fi

info "Override written to $OVERRIDE_DIR/local.conf"

systemctl daemon-reload
systemctl enable "$SERVICE"

# ── Start / restart ────────────────────────────────────────────────────────────
step "Starting service…"
systemctl restart "$SERVICE"
sleep 3

if systemctl is-active --quiet "$SERVICE"; then
	info "Service is running"
else
	warn "Service did not start - check: journalctl -u $SERVICE -n 50"
	systemctl status "$SERVICE" --no-pager -l || true
	exit 1
fi

# ── HTTP health check ──────────────────────────────────────────────────────────
step "Waiting for HTTP API…"
for i in $(seq 1 10); do
	if curl -sf "http://127.0.0.1:${HTTP_PORT}/api/stats" >/dev/null 2>&1; then
		info "HTTP API is up"
		break
	fi
	sleep 1
	[[ $i -eq 10 ]] && warn "API did not respond in time - it may still be starting"
done

# ── Summary ────────────────────────────────────────────────────────────────────
HOST_IP="$(hostname -I | awk '{print $1}')"
echo ""
echo -e "${CYAN}━━━  Setup complete  ━━━${NC}"
echo ""
echo -e "  Dashboard : ${GREEN}http://${HOST_IP}:${HTTP_PORT}/${NC}"
echo -e "  API docs  : ${GREEN}http://${HOST_IP}:${HTTP_PORT}/docs${NC}"
echo -e "  Status    : ${GREEN}http://${HOST_IP}:${HTTP_PORT}/api/status${NC}"
echo -e "  Logs      : journalctl -u ${SERVICE} -f"
echo -e "  Config    : ${OVERRIDE_DIR}/local.conf"
echo -e "  Data      : ${DATA_DIR}/data.db"
echo ""
echo -e "  Useful commands:"
echo -e "    sudo systemctl restart ${SERVICE}"
echo -e "    sudo systemctl stop    ${SERVICE}"
echo -e "    sudo bash tools/install-rpi-service.sh --uninstall"
echo ""
