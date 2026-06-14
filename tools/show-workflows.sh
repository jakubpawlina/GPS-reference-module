#!/usr/bin/env bash
set -Eeuo pipefail

cat <<'EOF'
GPS Reference Module workflows

First setup:
  mise install
  mise run firmware:bootstrap
  mise run service:bootstrap

During development:
  mise run test:unit          Fast firmware logic feedback
  mise run test:integration   Firmware runtime with simulated peripherals
  mise run test:simulation    Wokwi project and custom GPS chip contracts
  mise run test:service       Raspberry Pi storage, API, and lifecycle behavior

Before committing:
  mise run test:all           All host-side tests; no Docker required

Before opening a pull request:
  mise run verify             All tests plus ESP32 and Wokwi builds; requires Docker

Interactive Wokwi:
  mise run simulation:build
  code simulation/wokwi
  Run "Wokwi: Start Simulator" in VS Code

Other task groups:
  firmware:*   Toolchain setup and standalone ESP32 compilation
  simulation:* Wokwi project generation and build
  docs:*       API documentation
  deploy:*     Raspberry Pi service installation
EOF
