/**
 * @file gps_reference_module.ino
 * @brief Arduino entrypoint — delegates immediately to firmware_runtime.
 *
 * This file exists solely because the Arduino build system requires a
 * `.ino` file.  All real behavior lives in `src/firmware_runtime.*`.
 */
#include "src/firmware_runtime.h"

void setup() {
  setupFirmware();
}

void loop() {
  loopFirmware();
}
