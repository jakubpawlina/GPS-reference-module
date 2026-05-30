#pragma once

#include <stdint.h>

#include <Adafruit_SSD1306.h>

#include "status_presentation.h"

/**
 * @brief OLED-specific hardware integration.
 *
 * This module owns the code that depends directly on the SSD1306 driver and
 * the I2C bus. It accepts a prepared DisplayModel so rendering stays separate
 * from presentation policy.
 */
namespace OledDisplay {

/** @brief Probe the configured I2C bus for a supported SSD1306 address. */
uint8_t probeOledAddress();
/** @brief Initialize the SSD1306 display and render the boot screen. */
bool initializeDisplay(Adafruit_SSD1306 &display, uint8_t &detectedAddress);
/** @brief Render the current display model to the OLED. */
void renderDisplay(Adafruit_SSD1306 &display, const StatusPresentation::DisplayModel &model);

}  // namespace OledDisplay
