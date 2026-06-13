# GPS Reference Module

A self-contained GPS reference station for a local testbed network. An ESP32
reads and parses NMEA sentences from a GPS receiver, drives a status display and
LEDs, and streams structured JSON over USB serial. A Raspberry Pi reads that
stream, stores every position record in SQLite, and exposes a REST/SSE API over
Ethernet.

```
GPS receiver ──> ESP32 ──[USB]──> Raspberry Pi ──[Ethernet]──> Clients
               (parse + display)  (store + serve)            (browser / scripts)
```

---

## Repository layout

```
gps-reference-module/
├── firmware/
│   └── gps_reference_module/
│       ├── gps_reference_module.ino   Arduino entrypoint
│       ├── README.md                  Firmware-local guide
│       ├── Doxyfile                   Firmware API docs config
│       └── src/                       Firmware implementation modules
├── service/
│   ├── main.py                        Entry point
│   ├── api.py                         FastAPI REST + SSE + dashboard
│   ├── database.py                    SQLite storage
│   ├── reader.py                      Serial reader (asyncio + pyserial)
│   ├── config.py                      Environment-variable configuration
│   ├── requirements.txt               Python dependencies
│   └── gps-reference.service          systemd unit
├── docs/
│   ├── design.md                      System design and architecture
│   ├── hardware.md                    Wiring, components, pin assignments
│   ├── firmware.md                    Build, upload, serial protocol
│   ├── api.md                         REST/SSE endpoint reference
│   └── deploy.md                      Deployment and configuration guide
├── tests/
│   └── firmware/                      Host-side firmware verification source
└── tools/
    ├── setup-firmware-toolchain.sh    Arduino core/library setup
    ├── run-firmware-checks.sh         Firmware test/compile/docs helper
    ├── install-rpi-service.sh         Raspberry Pi install/uninstall helper
    └── generate-wokwi-project.py      Wokwi project generator
```

---

## Quick start

### 0 - Install developer tools

The repo uses [`mise`](https://mise.jdx.dev/) to pin the development toolchain.

```bash
mise install
mise run firmware:bootstrap
```

### 1 - Flash the ESP32

```bash
mise run firmware:compile
arduino-cli upload --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 firmware/gps_reference_module
```

### 2 - Deploy the Raspberry Pi service

```bash
# Copy files to the RPi
scp -r service tools pi@<rpi-ip>:~/gps-reference/

# On the RPi - run as root, pass your username
ssh pi@<rpi-ip>
cd ~/gps-reference
mise run deploy:install
```

See `docs/deploy.md` for offline installation (no internet on the RPi).

### 3 - Connect and verify

Plug the ESP32 into the RPi via USB. Open a browser:

```
http://<rpi-ip>:8000/            Live dashboard
http://<rpi-ip>:8000/docs        Swagger UI - try every endpoint
http://<rpi-ip>:8000/api/status  Current GPS state (JSON)
```

---

## Diagnostic states

| State | LED | Meaning |
|-------|-----|---------|
| `REFERENCE_OK` | Blue + Green | 3D fix, ≥6 satellites, all NMEA data fresh |
| `DEGRADED_LOW_SAT` | Blue + Yellow | Fix active but fewer than 6 satellites or GGA stale |
| `DEGRADED_2D` | Blue + Yellow | 2D fix only (no altitude) |
| `NO_FIX` | Red + Blue | NMEA arriving but no position fix yet |
| `NO_GPS_DATA` | Red | No NMEA received from the GPS receiver |

---

## Further reading

| Document | Contents |
|----------|----------|
| [docs/design.md](docs/design.md) | System architecture, design decisions, implementation notes |
| [docs/hardware.md](docs/hardware.md) | Wiring diagrams, full pin table, adjacency warnings |
| [docs/firmware.md](docs/firmware.md) | Build instructions, serial protocol, configuration constants |
| [docs/api.md](docs/api.md) | Complete REST/SSE endpoint reference with examples |
| [docs/deploy.md](docs/deploy.md) | Deployment, configuration, service management, troubleshooting |

---

## Simulation

The repository tracks one firmware source and refreshes one ignored Wokwi
project from it when needed.

- `simulation/assets/` holds the canonical Wokwi diagram, library list, and custom GPS chip.
- `tools/generate-wokwi-project.py` rebuilds `simulation/wokwi/` from the real firmware.

To build the local Wokwi for VS Code project, including the ESP32 firmware and
custom GPS chip:

```bash
mise run simulation:build
```

Open `simulation/wokwi/` in VS Code and run **Wokwi: Start Simulator**.

## Firmware quality workflow

The firmware project includes a small quality wrapper so the common checks stay
predictable:

```bash
mise run firmware:test
mise run firmware:compile
mise run firmware:verify
```

API docs can be generated with:

```bash
mise run firmware:docs
mise run docs:serve
```
