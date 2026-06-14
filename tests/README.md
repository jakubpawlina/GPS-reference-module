# Test Architecture

The test suite is organized by execution boundary rather than by implementation
language.

## Recommended Flow

1. Run the narrowest relevant layer while developing.
2. Run `mise run test:all` before committing. It is fast and does not require
   Docker.
3. Run `mise run verify` before opening a pull request. It adds the real ESP32
   and Wokwi WebAssembly builds.

| Layer | Command | Scope | External services |
|---|---|---|---|
| Unit | `mise run test:unit` | Pure NMEA parsing, state evaluation, framing, and presentation | None |
| Integration | `mise run test:integration` | Real firmware runtime with host-side UART, I2C, OLED, GPIO, serial, and clock fakes | None |
| Simulation | `mise run test:simulation` | Wokwi project assets, generator behavior, wiring contract, and custom GPS chip output | None |
| Service | `mise run test:service` | SQLite persistence and cleanup, API validation, and serial-reader lifecycle | None |
| All tests | `mise run test:all` | Every host-side test layer | None |
| Format | `mise run format:check` | Python (ruff), C++ (clang-format), and shell (shfmt) formatting gate | None |
| Lint | `mise run lint` | Python (ruff check) and shell static analysis | None |
| Type check | `mise run typecheck` | Python service | None |
| Full verification | `mise run verify` | All tests plus ESP32 and Wokwi WebAssembly builds | Docker for the custom chip build |

All compiled host tests use strict warnings. C++ tests additionally run with
AddressSanitizer and UndefinedBehaviorSanitizer.

Test output includes named cases, expected integration states, per-step
durations, suite totals, and a combined total. ANSI colors are enabled
automatically for interactive terminals. Set `NO_COLOR=1` for plain CI logs or
`FORCE_COLOR=1` when a terminal wrapper does not expose TTY detection.

The generated Wokwi project also contains an interactive acceptance procedure
for manually observing the OLED, LEDs, and serial monitor in the VS Code Wokwi
extension. Automated execution in the Wokwi cloud requires the optional
`wokwi-cli` and a `WOKWI_CLI_TOKEN`; it is intentionally not part of the local
test baseline.
