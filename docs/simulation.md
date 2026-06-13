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

## Wokwi Arduino project

Generate the local project first:

```bash
mise run simulation:generate
```

Then open `simulation/wokwi/` as a Wokwi Arduino project. The
simulated GPS receiver is a custom `chip-neo-m8n` component and provides a
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
mise run simulation:generate
```
