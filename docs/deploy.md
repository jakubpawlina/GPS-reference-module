# Deployment guide

## Requirements

- Raspberry Pi 4 (any RAM) running Raspberry Pi OS Bookworm / Debian Trixie
- Python 3.9 or newer (`python3 --version`)
- The ESP32 connected via USB

---

## Install

Copy the service files and install helper to the Raspberry Pi and run the
install script as root:

```bash
# From the development machine
ssh pi@<rpi-ip> 'mkdir -p ~/gps-reference'
scp -r service tools pi@<rpi-ip>:~/gps-reference/

# On the Raspberry Pi
cd ~/gps-reference
./tools/deploy-rpi-service.sh
```

The script performs every step automatically:

1. Checks Python version and source files
2. Installs system packages (`python3-venv`, `python3-dev`)
3. Creates `/opt/gps-reference/` (application) and `/var/lib/gps-reference/` (database)
4. Sets up a Python virtual environment and installs dependencies
5. Runs an import smoke test
6. Adds the app user to the `dialout` group
7. Installs and enables the systemd service
8. Waits for the HTTP API to respond and prints the URLs

### Offline installation (no internet on the RPi)

Download the wheels on a machine that has internet access:

```bash
pip download \
  --dest ./wheels \
  --platform linux_aarch64 \
  --python-version 313 \
  --only-binary=:all: \
  fastapi uvicorn aiosqlite pyserial httpx
```

Copy the wheels to the RPi and install:

```bash
scp wheels/*.whl pi@<rpi-ip>:~/gps-pkgs/
ssh pi@<rpi-ip>
sudo /opt/gps-reference/venv/bin/pip install \
  --no-index --find-links ~/gps-pkgs ~/gps-pkgs/*.whl
```

Then install only the non-Python parts of setup:

```bash
# On the RPi (as root)
sudo cp ~/gps-reference/service/gps-reference.service /etc/systemd/system/
sudo systemctl edit gps-reference
# Add User=<your-user> and Group=<your-user> under [Service].
sudo systemctl daemon-reload
sudo systemctl enable --now gps-reference
```

---

## Configuration

The installer writes generated settings to:

```
/etc/systemd/system/gps-reference.service.d/10-generated.conf
```

Place administrator-owned settings and secrets in:

```
/etc/systemd/system/gps-reference.service.d/local.conf
```

The installer never overwrites `local.conf`, so upgrades preserve local
authentication, bind-address, CORS, and webhook settings.

| Variable | Default | Description |
|----------|---------|-------------|
| `GPS_SERIAL_PORT` | `/dev/ttyUSB0` | Serial port the ESP32 is connected to |
| `GPS_BAUD_RATE` | `115200` | Must match `UsbConfig::BAUD_RATE` in firmware |
| `GPS_SERIAL_MAX_LINE_BYTES` | `4096` | Maximum accepted serial NDJSON record size |
| `GPS_STATE_STALE_SECONDS` | `3` | Seconds without a parsed state before live data becomes unavailable |
| `GPS_DB_PATH` | `/var/lib/gps-reference/data.db` | SQLite database path |
| `GPS_MAX_DB_BYTES` | `4294967296` | Storage cap in bytes (default 4 GB) |
| `GPS_MAX_SSE_CONNECTIONS` | `32` | Maximum concurrent dashboard event streams |
| `GPS_HTTP_HOST` | `0.0.0.0` | Listen address for the HTTP server |
| `GPS_HTTP_PORT` | `8000` | Listen port |
| `GPS_CORS_ORIGINS` | `*` | Comma-separated list of allowed CORS origins |
| `GPS_API_KEY` | _(empty)_ | Optional Bearer token for write endpoints (see [API key authentication](#api-key-authentication)) |
| `GPS_CLOUD_WEBHOOK` | _(empty)_ | Administrator-controlled HTTP(S) destination for `POST /api/upload` |

After editing, reload and restart:

```bash
sudo systemctl daemon-reload
sudo systemctl restart gps-reference
```

### Using GPIO instead of USB

If the ESP32 is connected via GPIO pins instead of USB:

```ini
# /etc/systemd/system/gps-reference.service.d/local.conf
[Service]
Environment=GPS_SERIAL_PORT=/dev/ttyAMA0
```

Disable the Raspberry Pi serial console first:

```bash
sudo raspi-config   # Interface Options → Serial Port → disable login shell, enable hardware port
```

---

## Service management

```bash
sudo systemctl status  gps-reference      # current state and last log lines
sudo systemctl restart gps-reference      # restart
sudo systemctl stop    gps-reference      # stop
sudo systemctl enable  gps-reference      # start on boot
sudo systemctl disable gps-reference      # do not start on boot

journalctl -u gps-reference -f            # follow live logs
journalctl -u gps-reference -n 100        # last 100 lines
```

---

## Uninstall

```bash
cd ~/gps-reference
mise run deploy:uninstall
```

This stops and removes the service and deletes `/opt/gps-reference/`. The
database in `/var/lib/gps-reference/` is kept and must be removed manually if
no longer needed.

---

## Upgrading

```bash
# Copy new files to the RPi
scp service/*.py service/requirements.txt service/gps-reference.service \
  pi@<rpi-ip>:~/gps-reference/service/

# On the RPi
cd ~/gps-reference
./tools/deploy-rpi-service.sh
```

The installer refreshes the application files, Python dependencies, systemd
unit, local override, permissions, and service state.

If `GPS_DB_PATH` points outside `/var/lib/gps-reference`, add that directory
to `ReadWritePaths` in a systemd override. The packaged unit enables
`ProtectSystem=strict`.

---

## Security hardening

> [!WARNING]
> The HTTP API binds to `0.0.0.0` by default with no authentication.
> Enable API key authentication or deploy behind a reverse proxy on
> non-trusted networks.

### API key authentication

Set `GPS_API_KEY` to require a Bearer token on write endpoints
(`POST /api/upload`). All read-only endpoints (status, stream, records,
stats) and the dashboard remain open.

```ini
# /etc/systemd/system/gps-reference.service.d/local.conf
[Service]
Environment=GPS_API_KEY=your-secret-token-here
```

Clients must include the token on protected write requests:

```bash
curl -X POST \
  -H "Authorization: Bearer your-secret-token-here" \
  "http://<rpi-ip>:8000/api/upload?since_cursor=0"
```

### Reverse proxy with nginx

Install nginx and create a site configuration:

```nginx
# /etc/nginx/sites-available/gps-reference
server {
    listen 443 ssl;
    server_name gps.example.lan;

    ssl_certificate     /etc/ssl/certs/gps-reference.pem;
    ssl_certificate_key /etc/ssl/private/gps-reference.key;

    # Basic authentication
    auth_basic           "GPS Reference";
    auth_basic_user_file /etc/nginx/.htpasswd;

    location / {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host $host;
        proxy_set_header X-Real-IP $remote_addr;
    }

    # SSE requires unbuffered proxying
    location /api/stream {
        proxy_pass http://127.0.0.1:8000;
        proxy_set_header Host $host;
        proxy_buffering off;
        proxy_cache off;
    }
}
```

Then bind the service to localhost only:

```ini
# /etc/systemd/system/gps-reference.service.d/local.conf
[Service]
Environment=GPS_HTTP_HOST=127.0.0.1
```

### Firewall

If a reverse proxy is not practical, restrict access at the firewall level:

```bash
sudo ufw allow from 192.168.1.0/24 to any port 8000 proto tcp
sudo ufw deny 8000
sudo ufw enable
```

---

## Troubleshooting

### Service does not start - `status=217/USER`

The selected deployment user does not exist. Create it or rerun the installer
with `GPS_APP_USER=<existing-user>`. The installer writes the selected account
to the systemd override.

### `/api/status` returns 503

The serial reader has not received fresh data yet. `/api/status` also returns
503 when the last parsed state is older than `GPS_STATE_STALE_SECONDS`. Check:

```bash
journalctl -u gps-reference -n 50
ls -la /dev/ttyUSB*
```

If no `/dev/ttyUSB*` exists, the ESP32 is not connected or the `cp210x`
kernel module is not loaded:

```bash
sudo modprobe cp210x
```

### Serial connected but no data (reader blocks)

The service intentionally uses pyserial through `asyncio.to_thread()` with a
one-second read timeout. Verify the deployed `reader.py` matches this
repository and that `GPS_BAUD_RATE` matches the firmware.

### Display shows nothing

1. Check `oledAddress` in the startup JSON. If `null`, the display was not found on I2C.
2. Verify wiring: GND, 3.3 V, SCL → GPIO22, SDA → GPIO19.
3. Ensure no LED wire is on GPIO18 (adjacent to SDA=GPIO19) - this causes I2C crosstalk.
