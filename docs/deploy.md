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
# From the dev machine
scp -r service tools pi@<rpi-ip>:~/gps-reference/

# On the Raspberry Pi
cd ~/gps-reference
mise run deploy:install
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
  fastapi uvicorn aiosqlite pyserial pyserial-asyncio httpx
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
sudo cp ~/gps-reference/gps-reference.service /etc/systemd/system/
sudo sed -i 's/User=pi/User=<your-user>/' /etc/systemd/system/gps-reference.service
sudo systemctl daemon-reload
sudo systemctl enable --now gps-reference
```

---

## Configuration

All settings are environment variables. Edit the systemd override file:

```
/etc/systemd/system/gps-reference.service.d/local.conf
```

| Variable | Default | Description |
|----------|---------|-------------|
| `GPS_SERIAL_PORT` | `/dev/ttyUSB0` | Serial port the ESP32 is connected to |
| `GPS_BAUD_RATE` | `115200` | Must match `UsbConfig::BAUD_RATE` in firmware |
| `GPS_DB_PATH` | `/var/lib/gps-reference/data.db` | SQLite database path |
| `GPS_MAX_DB_BYTES` | `4294967296` | Storage cap in bytes (default 4 GB) |
| `GPS_HTTP_HOST` | `0.0.0.0` | Listen address for the HTTP server |
| `GPS_HTTP_PORT` | `8000` | Listen port |
| `GPS_CLOUD_WEBHOOK` | _(empty)_ | URL for `POST /api/upload` if no `webhook_url` is provided |

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
scp service/*.py pi@<rpi-ip>:~/gps-reference/

# On the RPi
sudo cp ~/gps-reference/*.py /opt/gps-reference/
sudo systemctl restart gps-reference
```

---

## Troubleshooting

### Service does not start - `status=217/USER`

The `User=` in the service file names a user that does not exist.
Edit `/etc/systemd/system/gps-reference.service` and change `User=pi`
to your actual username, then `sudo systemctl daemon-reload && sudo systemctl restart gps-reference`.

### `/api/status` returns 503

The serial reader has not received any data yet. Check:

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

`pyserial-asyncio` has a known incompatibility with Python 3.13 where
`readline()` blocks indefinitely. The `reader.py` in this repository uses
`asyncio.to_thread(ser.readline)` instead, which avoids this issue.
Verify the deployed file matches `service/reader.py`.

### Display shows nothing

1. Check `oledAddress` in the startup JSON. If `null`, the display was not found on I2C.
2. Verify wiring: GND, 3.3 V, SCL → GPIO22, SDA → GPIO19.
3. Ensure no LED wire is on GPIO18 (adjacent to SDA=GPIO19) - this causes I2C crosstalk.
