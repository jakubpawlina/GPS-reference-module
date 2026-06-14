# Simulation

This repository includes a generated Wokwi simulation for the ESP32 GPS
reference module. The production firmware remains in
`firmware/gps_reference_module/`; `simulation/` contains the canonical assets
and one generator used to build a runnable local Wokwi project.

## What is included

```text
simulation/
├── assets/                # Canonical shared Wokwi assets
│   ├── diagram.json       # ESP32 + OLED + LEDs + simulated GPS wiring
│   ├── libraries.txt      # Arduino library dependencies
│   ├── neo-m8n.chip.c     # custom GPS simulator chip
│   └── neo-m8n.chip.json  # custom GPS simulator UI controls
└── wokwi/                 # Ignored generated Wokwi project, created on demand
    ├── sketch.ino         # Copied firmware entrypoint
    ├── wokwi.toml         # Wokwi for VS Code configuration
    ├── build/             # Compiled ESP32 firmware artifacts
    ├── neo-m8n.chip.wasm  # Compiled custom GPS simulator
    └── src/               # Complete copied firmware implementation
```

The project generator lives in
[`tools/generate-wokwi-project.py`](../tools/generate-wokwi-project.py).
It discovers the single top-level `.ino` entrypoint and recursively copies the
firmware and asset directories while excluding files ignored by Git. Alternative
projects can be supplied with `--firmware-dir`, `--assets-dir`, and
`--output-dir`.

## Simulated wiring

The simulation follows the current firmware pin map:

| Function | ESP32 GPIO |
|---|---:|
| GPS TX → ESP32 RX | 16 |
| GPS RX ← ESP32 TX | 17 |
| OLED SDA | 19 |
| OLED SCL | 22 |
| Red error LED | 23 |
| Blue data LED | 21 |
| Yellow warning LED | 5 |
| Green OK LED | 25 |

## Wokwi for VS Code

Install Docker and the
[Wokwi for VS Code extension](https://marketplace.visualstudio.com/items?itemName=Wokwi.wokwi-vscode).
The first simulation build downloads the official `wokwi/builder-clang-wasm`
container image used to compile the custom GPS chip.

Build the complete local simulation project:

```bash
mise run simulation:build
```

Open `simulation/wokwi/` as the VS Code workspace. Press `F1` and select
**Wokwi: Start Simulator**. After firmware changes, run the default VS Code
build task (`Ctrl+Shift+B`) before restarting the simulator.

Use `simulation/wokwi/TESTING.md` as the interactive acceptance test. It lists
the expected OLED text, LED pattern, and parsed serial state for every GPS
scenario. This exercises the compiled ESP32 firmware inside Wokwi and is the
closest simulation equivalent to observing the physical module.

The simulated GPS receiver is a custom `chip-neo-m8n` component and provides a
**Scenario** control:

- `0` = Auto demo
- `1` = No GPS data
- `2` = GPS data / No fix
- `3` = 2D fix / Warning
- `4` = 3D fix / Low satellites
- `5` = Reference OK

The ESP32 serial monitor emits the same JSON Lines protocol as the real module:
startup JSON, raw NMEA JSON, and parsed state JSON.

## Deploy Firmware

Simulation generation is separate from deployment. Build or upload the
physical ESP32 firmware with the normal commands documented in `docs/firmware.md`.

## Regenerating after firmware changes

The generated Wokwi project is a disposable copy that is excluded from Git.
After changing any file under `firmware/gps_reference_module/`, regenerate:

```bash
mise run simulation:build
```

Use `mise run simulation:generate` only when source files are needed without
compiled firmware and custom-chip artifacts.

## Validation

Run fast source-level checks without Docker:

```bash
mise run test:simulation
```

This validates the Wokwi JSON/TOML configuration, firmware-to-diagram pin
mapping, generator behavior, ignored-file handling, and custom GPS NMEA output.

Run the firmware runtime integration test:

```bash
mise run test:integration
```

This compiles the real firmware runtime with host-side Arduino, UART, I2C,
OLED, GPIO, and clock fakes. It feeds the same GPS states used by the Wokwi
custom chip and verifies startup configuration, serial JSON, OLED output, raw
sentence counts, and status LEDs for `NO DATA`, `NO FIX`, `2D`, low-satellite,
and healthy-reference scenarios.

Run the full verification workflow, including firmware and WebAssembly builds:

```bash
mise run verify
```

The final step verifies that the generated ESP32 BIN, ELF, and custom-chip WASM
files exist and have the expected binary signatures.
