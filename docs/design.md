# Design Document

## 1. Idea

A permanent GPS reference station for a local testbed network. The station
provides a continuously-updated, network-accessible source of position truth
that other devices on the network can poll or subscribe to without any
dependency on cloud services or external connectivity.

Use cases:
- Position reference for field tests requiring accurate ground truth
- Timestamp source correlated with a hardware GPS clock
- Persistent logging of position data for post-test analysis
- Live monitoring via a browser with no installation

---

## 2. Specifications

### System

| Property | Value |
|----------|-------|
| Position source | GPS/GNSS receiver, NMEA 0183 |
| Update rate | 1 Hz (one record per second) |
| Position accuracy | ~1-3 m CEP (receiver-dependent) |
| Storage capacity | Up to 4 GB SQLite (~97 days at 1 Hz) |
| Network interface | REST/SSE over HTTP on port 8000 |
| Display | 128×64 OLED, 8 status rows, 1-second refresh |
| Status indication | 4 LEDs encoding 5 diagnostic states |

### Firmware

| Property | Value |
|----------|-------|
| Platform | ESP32-D0WD-V3, 240 MHz dual-core |
| NMEA sentences parsed | GGA, GSA, RMC |
| GPS baud rate | 9600 |
| USB serial output | 115200, NDJSON |
| I2C clock | 100 kHz |
| Loop rate | 200 Hz (5 ms delay), display/JSON at 1 Hz |

### Service

| Property | Value |
|----------|-------|
| Language | Python 3.9+ |
| HTTP framework | FastAPI + uvicorn |
| Database | SQLite with WAL journalling |
| Serial | pyserial via asyncio.to_thread() |
| Shutdown time | ≤ 5 s (no SIGKILL required) |

---

## 3. Bill of materials

| Qty | Part | Notes |
|-----|------|-------|
| 1 | ESP32-D0WD-V3 dev board, 38-pin | CP2102 USB-UART bridge |
| 1 | GPS/GNSS module | NMEA 0183, 9600 baud (Neo-6M, Neo-8M, Neo-M8N) |
| 1 | 0.96" SSD1306 OLED, I2C, 4-pin | Must include onboard 4.7 kΩ pull-ups |
| 4 | 5 mm LED | Red, blue, yellow, green |
| 4 | Resistor 220-330 Ω | Current limiting, one per LED |
| 1 | Raspberry Pi 4 Model B | Any RAM; runs the REST service |
| 1 | USB cable, micro-USB to USB-A | ESP32 ↔ RPi |

---

## 4. System architecture

```
┌─────────────────────────────────┐     USB/serial (115200 baud, NDJSON)
│           ESP32                 │ ───────────────────────────────────────
│                                 │                                        │
│  GPS RX (9600 baud) → parser    │                                        ▼
│  Parser → DiagnosticState       │                         ┌─────────────────────────┐
│  DiagnosticState → OLED + LEDs  │                         │      Raspberry Pi       │
│  All state → USB JSON stream    │                         │                         │
└─────────────────────────────────┘                         │  reader.py              │
                                                            │    pyserial thread      │
                                                            │    → database.insert()  │
                                                            │                         │
                                                            │  SQLite (WAL)           │
                                                            │    positions table      │
                                                            │                         │
                                                            │  api.py (FastAPI)       │
                                                            │    REST endpoints       │
                                                            │    SSE stream           │
                                                            │    browser dashboard    │
                                                            └──────────┬──────────────┘
                                                                       │ Ethernet (HTTP :8000)
                                                                  ┌────▼────┐
                                                                  │ Clients │
                                                                  └─────────┘
```

---

## 5. Firmware design

### NMEA parsing

Three sentence types are processed:

| Sentence | Fields extracted |
|----------|-----------------|
| GGA | Time, lat/lon, fix quality, satellites used, HDOP, altitude, geoid separation |
| GSA | Fix type (2D/3D), PDOP, HDOP, VDOP |
| RMC | Time, date, lat/lon, speed, course, active/void flag |

Each sentence type has an independent freshness timestamp. A sentence is
considered stale after `GPS_DATA_TIMEOUT_MS` (1800 ms). The diagnostic state
is computed from a point-in-time snapshot of all freshness flags.

### Diagnostic state machine

```
freshNmea?
  No  → NO_GPS_DATA  (red LED)
  Yes → usablePosition?
          No  → NO_FIX         (red + blue)
          Yes → fixType == 2D?
                  Yes → DEGRADED_2D     (blue + yellow)
                  No  → satellites < 6?
                          Yes → DEGRADED_LOW_SAT  (blue + yellow)
                          No  → REFERENCE_OK      (blue + green)
```

### USB serial output

Three newline-delimited JSON message types:

- **`startup`** - emitted once at boot; contains firmware version, baud rates,
  detected OLED address, and display ready flag.
- **`raw_nmea`** - emitted for every received NMEA sentence; contains the
  original sentence, sentence type, and checksum result.
- **`parsed_state`** - emitted every second; contains the complete GPS state
  including all validity flags, coordinates, quality metrics, and counters.

### LED control

LEDs are active-high (GPIO → resistor → LED anode → cathode → GND). The
off-state is pin-specific due to adjacency constraints:

| Pin | Off state | Rationale |
|-----|-----------|-----------|
| GPIO21 (blue), GPIO23 (red) | `INPUT_PULLDOWN` | Adjacent to SDA/SCL - OUTPUT LOW would pull the I2C bus low |
| GPIO5 (yellow), GPIO25 (green) | `OUTPUT LOW` | Not adjacent to I2C - OUTPUT LOW is safe and prevents dim glow from neighbouring driven signals |

`INPUT (floating)` was rejected for all pins: driven neighbours (GPS_TX idling
HIGH, I2C pull-ups) leak enough current to dimly illuminate a floating LED.
`INPUT_PULLDOWN` (47 kΩ) was rejected for GPIO5 because GPS_TX idles
continuously at 3.3 V and the divider keeps the pin well above the LED
forward-voltage threshold. See `hardware.md` for the full adjacency table.

### OLED initialisation

`Adafruit_SSD1306::begin()` always returns `true` if memory allocation
succeeds, regardless of whether a display is physically present. A manual I2C
probe (`Wire.beginTransmission()` + `Wire.endTransmission() == 0`) is performed
before calling `begin()` to distinguish "no display" from "malloc failed".

---

## 6. Service design

### Reader (`reader.py`)

The serial reader runs entirely in a thread pool (`asyncio.to_thread()`) to
avoid blocking the event loop. The implementation addresses four constraints:

1. **pyserial-asyncio 0.6 compatibility** - does not feed data to
   `asyncio.StreamReader` on Python 3.13. Plain pyserial is used instead.

2. **Graceful shutdown** - `asyncio.to_thread()` tasks cannot be cancelled
   while a thread is blocking inside `readline()`. A `threading.Event` stop
   flag with a 1-second `readline()` timeout allows the thread to exit within
   ~1 s of a shutdown signal.

3. **ESP32 reset prevention** - opening a CP2102 serial port toggles DTR,
   which resets the ESP32. `dsrdtr=False, rtscts=False` suppress this.

4. **Stalled port detection** - if no data arrives within 10 s of opening the
   port, the port is closed and reopened (recovers a rare Linux kernel condition
   where the file descriptor stalls silently).

5. **Freshness and input bounds** - serial records are limited to 4096 bytes by
   default, and live state expires after 3 seconds without a new `parsed_state`.
   The API returns 503 rather than presenting a disconnected receiver's last fix
   as current.

6. **Supervisor resilience** - malformed JSON values are discarded and
   unexpected processing or storage failures are logged and retried instead of
   terminating the background reader task.

### Shutdown (`main.py`)

uvicorn's own `handle_exit` method is patched to set the reader's stop flag
immediately on SIGTERM/SIGINT, before uvicorn begins waiting for connections.
`timeout_graceful_shutdown=3` forces SSE connections closed after 3 s.
`TimeoutStopSec=10` in the systemd unit provides a hard backstop.

### Database (`database.py`)

SQLite in WAL mode with `PRAGMA synchronous = NORMAL`. One record per second
occupies approximately 400-600 bytes; 4 GB supports ~97 days of continuous
recording. When the database and WAL files reach 95 % of the cap, the oldest
5 % of rows are deleted and SQLite reclaims the freed pages.

### API (`api.py`)

| Pattern | Endpoint | Notes |
|---------|----------|-------|
| Live state | `GET /api/status` | Reads in-memory dict, no DB hit |
| Live push | `GET /api/stream` | SSE; 1-second poll of in-memory state |
| Incremental poll | `GET /api/records/since` | Cursor = last row id; stateless server |
| Historical | `GET /api/records/range` | Unix timestamp window |
| Upload | `POST /api/upload` | HTTP POST to configured webhook |

The cursor-based polling design is intentionally stateless: the server never
stores session state. Clients save `next_cursor` from each response and present
it on the next call, making the protocol resilient to client restarts.

---

## 7. Key engineering decisions

### Why ESP32 + RPi instead of RPi only?

The ESP32 handles the hard-real-time requirements: UART reception at 9600 baud,
NMEA checksum validation, LED and display driving. The RPi handles the
computationally simple but availability-critical requirements: persistent
storage, network serving, and process supervision via systemd. Separating these
concerns also means the GPS module is always accumulating a fix even when the
RPi service restarts.

### Why NDJSON over USB serial?

NDJSON is human-readable, trivially parseable in any language, and the existing
USB connection is free. An SPI or CAN bus would be faster but would require
additional hardware and make the ESP32 output harder to debug.

### Why SQLite?

Sufficient write throughput for 1 Hz, zero operational overhead, and the
resulting `.db` file is directly portable for offline analysis. PostgreSQL or
InfluxDB would add deployment complexity with no benefit at this scale.

### Why cursor-based polling instead of WebSockets?

Cursor-based polling over plain HTTP is simpler to implement on heterogeneous
clients (Python scripts, shell scripts, browsers, embedded devices). The SSE
endpoint covers the case where a persistent connection and low latency are
needed.

---

## 8. Known limitations

- **Single GPS receiver.** No redundancy; a receiver fault means no position.
- **Read endpoints are unauthenticated.** `GPS_API_KEY` optionally protects
  `POST /api/upload`, but the dashboard and read-only API remain open. Use an
  authenticated reverse proxy when the network is not trusted.
- **Clock drift.** Record timestamps use the RPi's system clock, which may
  drift without NTP. GPS UTC time is stored in `utc_time`/`utc_date` fields
  and can be used as an accurate reference.
- **4 GB storage cap.** Configured to delete oldest data automatically. Adjust
  `GPS_MAX_DB_BYTES` or archive periodically for long-term logging.
- **NMEA-only.** Raw binary protocols (UBX, SiRF) are not supported.
