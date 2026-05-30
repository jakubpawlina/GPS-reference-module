# Firmware Project

This directory is a standalone Arduino firmware project for the ESP32-based GPS
reference module.

## Layout

```text
gps_reference_module/
├── gps_reference_module.ino   Arduino entrypoint only
├── Doxyfile                   API documentation configuration
├── README.md                  Local project guide
└── src/
    ├── firmware_runtime.*         Runtime orchestration
    ├── firmware_settings.h        Centralized constants and pin map
    ├── gps_processing.*           NMEA parsing and GPS state evaluation
    ├── nmea_stream_framer.*       Serial-byte to sentence framing
    ├── status_presentation.*      OLED/LED presentation mapping
    ├── oled_display.*             SSD1306 rendering and I2C probing
    ├── status_led_controller.*    GPIO LED control
    └── serial_json_reporter.*     USB JSON Lines output
```

## Design rules

- `gps_reference_module.ino` stays trivial. All real behavior lives in `src/`.
- Pure logic remains Arduino-free where practical, so host-side tests stay fast.
- Hardware modules take already-derived view models instead of re-encoding
  firmware rules internally.
- Public headers are documented with Doxygen comments. Internal helper functions
  stay in `.cpp` files.

## Local quality checks

Install the pinned toolchain and Arduino dependencies first:

```bash
mise install
mise run firmware:bootstrap
```

Run the host-side verification suite:

```bash
mise run firmware:test
```

Run an ESP32 compile using `arduino-cli`:

```bash
mise run firmware:compile
```

Run the normal verification set in one step:

```bash
mise run firmware:verify
```

Generate API documentation:

```bash
mise run firmware:docs
```
