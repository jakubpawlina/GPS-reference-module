# GPS Reference Module Firmware

ESP32 firmware that reads a NEO-6M GPS receiver over UART, parses NMEA 0183
sentences, and exposes diagnostic state through an OLED display, four status
LEDs, and a USB JSON Lines stream consumed by the Raspberry Pi service.

## Architecture

```text
NEO-6M GPS  ──UART 9600──▶  NmeaStreamFramer  ──▶  GpsProcessing
                             (byte → sentence)       (NMEA → GpsData)
                                                          │
                                ┌─────────────────────────┤
                                ▼                         ▼
                        StatusPresentation          buildGpsSnapshot()
                        (GpsData → models)          (GpsData → Snapshot)
                           │         │                    │
                           ▼         ▼                    ▼
                     OledDisplay  StatusLedController  SerialJsonReporter
                     (I2C SSD1306)  (4x GPIO LED)      (USB JSON Lines)
```

## Modules

| Module | Header | Role |
|--------|--------|------|
| **GpsProcessing** | gps_processing.h | NMEA checksum, GGA/GSA/RMC parsing, coordinate conversion, diagnostic state machine |
| **NmeaStreamFramer** | nmea_stream_framer.h | Accumulates raw UART bytes into complete `$...*XX` sentences |
| **StatusPresentation** | status_presentation.h | Converts GpsData + Snapshot into DisplayModel (OLED rows) and LedPattern (GPIO booleans) |
| **OledDisplay** | oled_display.h | I2C address probing, SSD1306 initialization, screen rendering |
| **StatusLedController** | status_led_controller.h | GPIO pin control for error / data / warning / ok LEDs |
| **SerialJsonReporter** | serial_json_reporter.h | Emits startup, raw_nmea, and parsed_state JSON over USB |
| **FirmwareRuntime** | firmware_runtime.h | Top-level orchestration: setup + main loop |
| **FirmwareSettings** | firmware_settings.h | Compile-time constants: pins, timing, thresholds |

## Data flow

1. **UART** &mdash; `gpsSerial` delivers bytes at 9600 baud from the NEO-6M.
2. **Framing** &mdash; `NmeaStreamFramer::LineAccumulator` buffers bytes until a
   complete `$...` sentence with CR/LF termination is assembled.
3. **Parsing** &mdash; `GpsProcessing::processNmeaSentence()` validates the
   checksum, tokenizes the CSV fields, and dispatches to parseGga / parseGsa /
   parseRmc which update the shared `GpsData` struct.
4. **Snapshot** &mdash; Once per tick, `buildGpsSnapshot()` evaluates which data
   sources are still fresh (within the 1.8 s timeout) and derives composite flags
   like `usablePosition` and `diagnosticState`.
5. **Output** &mdash; The snapshot drives three independent consumers:
   - **OLED** refreshes at 1 Hz via `StatusPresentation::buildDisplayModel()`.
   - **LEDs** update every tick via `StatusPresentation::buildLedPattern()`.
   - **USB JSON** emits a `parsed_state` record at 1 Hz.

## Diagnostic states

| State | Meaning | LED pattern |
|-------|---------|-------------|
| `REFERENCE_OK` | 3D fix, &ge;6 satellites, all data fresh | ok=ON |
| `DEGRADED_LOW_SAT` | Fix acquired, &lt;6 satellites or GGA stale | warning=ON |
| `DEGRADED_2D` | Fix acquired, 2D only (no altitude) | warning=ON |
| `NO_FIX` | NMEA arriving but no position fix | error=ON |
| `NO_GPS_DATA` | No NMEA received from the receiver | error=ON |

## Building and testing

Install the toolchain and Arduino dependencies:

```bash
mise install
mise run firmware:bootstrap
```

Run host-side unit tests (no hardware needed):

```bash
mise run test:unit
```

Compile for ESP32:

```bash
mise run firmware:compile
```

Generate and serve this API documentation:

```bash
mise run docs:serve
```
