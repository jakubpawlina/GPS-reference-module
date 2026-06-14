/**
 * @file status_led_controller.h
 * @brief GPIO LED driver for the four status indicators.
 */
#pragma once

#include "status_presentation.h"

/** @brief GPIO-level control for the four status LEDs. */
namespace StatusLedController {

/** @brief Put all LED pins into the defined startup-off state. */
void initializeLeds();
/** @brief Apply one logical LED pattern to the physical pins. */
void applyPattern(const StatusPresentation::LedPattern &pattern);

} // namespace StatusLedController
