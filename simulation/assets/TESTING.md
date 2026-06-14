# Interactive Wokwi Test

This test runs the compiled ESP32 firmware in Wokwi and lets you inspect the
same outputs that are available on physical hardware: LEDs, OLED content, and
USB serial messages.

## Start

1. Open this generated `simulation/wokwi/` directory in Visual Studio Code.
2. Accept the recommended Wokwi extension if it is not already installed.
3. Press `Ctrl+Shift+B` to rebuild the firmware and custom GPS chip.
4. Press `F1` and run **Wokwi: Start Simulator**.
5. Keep the Serial Monitor and simulated hardware visible.

The GPS chip starts in automatic mode and advances to the next state every
eight simulated seconds. Select the GPS component and change its **Scenario**
control to hold a specific state.

## Acceptance Matrix

| Scenario | OLED state | Red | Blue data | Yellow | Green | Parsed serial state |
|---:|---|:---:|:---:|:---:|:---:|---|
| `1` No data | `NO DATA` | On | Off | Off | Off | `NO_GPS_DATA` |
| `2` No fix | `NO FIX` | On | On | Off | Off | `NO_FIX` |
| `3` 2D fix | `WARN 2D` | Off | On | On | Off | `DEGRADED_2D` |
| `4` Low satellites | `LOW SAT` | Off | On | On | Off | `DEGRADED_LOW_SAT` |
| `5` Reference OK | `OK` | Off | On | Off | On | `REFERENCE_OK` |

For every scenario:

- The OLED must update within about one second.
- The serial monitor must continue producing valid JSON Lines.
- `raw_nmea` messages must appear for scenarios `2` through `5`.
- `parsed_state` must contain the state shown in the table.
- Scenario `5` must show coordinates, altitude, HDOP, and at least nine
  satellites on the OLED and in serial output.

Scenario `0` is the automatic demonstration sequence:

`NO DATA` -> `NO FIX` -> `WARN 2D` -> `LOW SAT` -> `OK`

## Failure Checks

The test fails if any of these are observed:

- OLED remains on `START` or is blank.
- More than one status LED group is active unexpectedly.
- Serial output stops, contains malformed JSON, or reports checksum errors.
- The OLED, LEDs, and `parsed_state.state` disagree.
