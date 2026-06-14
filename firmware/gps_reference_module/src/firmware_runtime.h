/**
 * @file firmware_runtime.h
 * @brief Public interface for the Arduino setup/loop delegation.
 */
#pragma once

/** @brief Initialize the firmware runtime and hardware peripherals. */
void setupFirmware();
/** @brief Execute one firmware main-loop iteration. */
void loopFirmware();
