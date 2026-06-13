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

The project generator lives in [`tools/generate-wokwi-project.py`](/home/jakub/wsei/GPS-reference-module/tools/generate-wokwi-project.py).
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
