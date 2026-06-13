# Firmware

## Overview

The firmware runs on the ESP32 and performs three tasks:

1. **Parse NMEA 0183** - reads GGA, GSA, and RMC sentences from the GPS receiver at 9600 baud on Serial2 (GPIO16/17). Validates checksums, tracks data freshness with a 1800 ms staleness window.
2. **Drive the OLED** - updates a 128×64 SSD1306 display over I2C (GPIO19/22) every second with current GPS state, coordinates, altitude, HDOP, and data age.
3. **Stream JSON over USB** - emits newline-delimited JSON at 115200 baud on the USB serial port for consumption by the Raspberry Pi service.

---

## Build requirements

- [`mise`](https://mise.jdx.dev/) for the pinned local toolchain
- [arduino-cli](https://arduino.github.io/arduino-cli/) or Arduino IDE 2.x
- ESP32 board package: `esp32:esp32`
- Libraries:
  - `Adafruit SSD1306`
  - `Adafruit GFX Library`

Install the toolchain and Arduino dependencies:

```bash
mise install
mise run firmware:bootstrap
```

---

## Build and upload

```bash
# Compile
mise run firmware:compile

# Upload (adjust port as needed)
arduino-cli upload --fqbn esp32:esp32:esp32 -p /dev/ttyUSB0 firmware/gps_reference_module
```

## Host-side logic tests

The firmware keeps its pure logic outside the Arduino runtime so it can be
tested on the host without ESP32 hardware. The host-side test target covers:

- NMEA checksum validation and sentence parsing
- diagnostic-state transitions
- serial line framing and overflow handling
- display/LED presentation mapping

Run the firmware logic tests with:

```bash
mise run firmware:test
```

These tests compile the pure firmware modules with the host `g++` compiler.

The helper scripts are still available directly if you do not want to use the
task aliases:

```bash
./tools/run-firmware-checks.sh test
./tools/run-firmware-checks.sh compile
```

API documentation can be generated with:

```bash
mise run firmware:docs
mise run docs:serve
```

---

## Configuration constants

All tunable values are centralized in
[`firmware_config.h`](/home/jakub/wsei/GPS-reference-module/firmware/gps_reference_module/src/firmware_config.h):

| Namespace | Constant | Default | Description |
|-----------|----------|---------|-------------|
| `GpsConfig` | `BAUD_RATE` | 9600 | GPS serial baud rate |
| `GpsConfig` | `MIN_OK_SATELLITES` | 6 | Satellites required for `REFERENCE_OK` state |
| `UsbConfig` | `BAUD_RATE` | 115200 | USB serial output baud rate |
| `TimingConfig` | `GPS_DATA_TIMEOUT_MS` | 1800 | Milliseconds before a sentence is considered stale |
| `TimingConfig` | `DISPLAY_REFRESH_MS` | 1000 | Display update interval |
| `NmeaConfig` | `REQUIRE_CHECKSUM` | false | Set to `true` to reject sentences without a checksum |
| `DisplayConfig` | `I2C_ADDRESS` | 0x3C | Primary OLED I2C address |
| `DisplayConfig` | `I2C_ADDRESS_ALT` | 0x3D | Fallback address (auto-detected at boot) |

---

## Diagnostic states

| State | Display | LED pattern | Meaning |
|-------|---------|-------------|---------|
| `REFERENCE_OK` | `OK` | blue + green | 3D fix, ≥6 satellites, all sentences fresh |
| `DEGRADED_LOW_SAT` | `LOW SAT` | blue + yellow | Fix but <6 satellites, or GGA stale |
| `DEGRADED_2D` | `WARN 2D` | blue + yellow | 2D fix only (no altitude) |
| `NO_FIX` | `NO FIX` | red + blue | NMEA arriving but no position fix |
| `NO_DATA` | `NO DATA` | red only | No NMEA received within timeout |

---

## USB serial output format

All output is newline-delimited JSON at 115200 baud. Three message types:

### `startup`

Emitted once at boot:

```json
{
  "type": "startup",
  "module": "gps-reference-module",
  "version": "1.2.0",
  "usbBaud": 115200,
  "gpsBaud": 9600,
  "rawNmeaJson": true,
  "oledAddress": "3C",
  "displayReady": true
}
```

### `raw_nmea`

Emitted for every received NMEA sentence:

```json
{
  "type": "raw_nmea",
  "millis": 12345,
  "checksumOk": true,
  "sentenceType": "GGA",
  "sentence": "$GNGGA,..."
}
```

`sentenceType` is `GGA`, `GSA`, `RMC`, or `UNKNOWN`.

### `parsed_state`

Emitted once per second with the full GPS state:

```json
{
  "type": "parsed_state",
  "millis": 12345,
  "state": "REFERENCE_OK",
  "valid": true,
  "gpsData": true,
  "displayReady": true,
  "fix": true,
  "fixType": "3D",
  "fixQuality": 2,
  "satellitesUsed": 12,
  "latitude": 50.026652,
  "longitude": 19.953602,
  "altitudeM": 263.1,
  "geoidSeparationM": 40.0,
  "hdop": 0.8,
  "pdop": 1.6,
  "vdop": 1.4,
  "speedKnots": 0.02,
  "speedKmh": 0.04,
  "courseDeg": null,
  "utcTime": "180810.00",
  "utcDate": "250526",
  "nmeaAgeMs": 135,
  "ggaAgeMs": 741,
  "gsaAgeMs": 638,
  "rmcAgeMs": 856,
  "rawSentenceCount": 2994,
  "acceptedSentenceCount": 2994,
  "checksumErrorCount": 0,
  "bufferOverflowCount": 0
}
```

Fields are `null` when the corresponding data is unavailable or stale.

---

## OLED display

The display is auto-detected at startup using an I2C bus scan across both
candidate addresses (0x3C and 0x3D) and all common SDA/SCL pin combinations.
The detected address is reported in the `startup` JSON message.

Display layout (8 rows, text size 1):

```
STATE : REFERENCE OK
FIX   : 3D
SATS  : 12
LAT   : 50.026652
LON   : 19.953602
ALT   : 263.1m
HDOP  : 0.8
AGE   : <1s
```

---

## Project structure

The firmware is now split by responsibility:

- [`gps_reference_module.ino`](/home/jakub/wsei/GPS-reference-module/firmware/gps_reference_module/gps_reference_module.ino)
  Arduino entrypoint only
- [`src/firmware_runtime.cpp`](/home/jakub/wsei/GPS-reference-module/firmware/gps_reference_module/src/firmware_runtime.cpp)
  runtime orchestration
- [`src/firmware_settings.h`](/home/jakub/wsei/GPS-reference-module/firmware/gps_reference_module/src/firmware_settings.h)
  centralized settings, pins, and timing values
- [`src/gps_processing.cpp`](/home/jakub/wsei/GPS-reference-module/firmware/gps_reference_module/src/gps_processing.cpp)
  pure parsing and GPS state logic
- [`src/nmea_stream_framer.cpp`](/home/jakub/wsei/GPS-reference-module/firmware/gps_reference_module/src/nmea_stream_framer.cpp)
  serial sentence framing
- [`src/status_presentation.cpp`](/home/jakub/wsei/GPS-reference-module/firmware/gps_reference_module/src/status_presentation.cpp)
  display and LED view-model logic
- [`src/oled_display.cpp`](/home/jakub/wsei/GPS-reference-module/firmware/gps_reference_module/src/oled_display.cpp)
  OLED hardware rendering
- [`src/status_led_controller.cpp`](/home/jakub/wsei/GPS-reference-module/firmware/gps_reference_module/src/status_led_controller.cpp)
  LED hardware control
- [`src/serial_json_reporter.cpp`](/home/jakub/wsei/GPS-reference-module/firmware/gps_reference_module/src/serial_json_reporter.cpp)
  USB JSON output

The split is intentional:

- pure modules in `src/` stay host-testable
- hardware-specific modules stay narrow and isolated
- the `.ino` stays stable and review-friendly even as the project grows
