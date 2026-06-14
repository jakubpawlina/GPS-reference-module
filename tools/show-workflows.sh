#!/usr/bin/env bash
set -Eeuo pipefail

cat <<'EOF'
GPS Reference Module — development workflows

First setup
  mise install
  mise run firmware:bootstrap
  mise run service:bootstrap

During development
  mise run service:dev          Run the service locally with fake GPS data
  mise run test:unit            Fast firmware logic tests
  mise run test:integration     Firmware runtime with simulated peripherals
  mise run test:simulation      Wokwi project and custom GPS chip tests
  mise run test:service         Service storage, API, and lifecycle tests

Code quality
  mise run format               Auto-format Python, C++, and shell
  mise run format:check         Verify formatting without changes (CI gate)
  mise run lint                 Python and shell static analysis
  mise run typecheck            Python service type checking

Before committing
  mise run test:all             All host-side tests (no Docker required)

Before opening a pull request
  mise run verify               All tests plus ESP32 and Wokwi builds (requires Docker)

Interactive Wokwi simulator
  mise run simulation:build
  code simulation/wokwi
  Run "Wokwi: Start Simulator" from the VS Code command palette

All task groups
  firmware:*    Toolchain setup and ESP32 compilation
  service:*     Local dev server and dependency bootstrap
  test:*        All test layers
  simulation:*  Wokwi project generation and build
  format / lint / typecheck
                 Code formatting and static analysis
  docs:*        Firmware API documentation
  deploy:*      Raspberry Pi service installation
EOF
