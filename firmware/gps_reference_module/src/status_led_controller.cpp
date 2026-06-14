#include "status_led_controller.h"

#include <Arduino.h>

#include "firmware_settings.h"

namespace StatusLedController {

namespace {

void setLedState(uint8_t pin, bool on) {
  if (on) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  } else if (pin == PinConfig::LED_DATA || pin == PinConfig::LED_ERROR) {
    pinMode(pin, INPUT_PULLDOWN);
  } else {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);
  }
}

} // namespace

void initializeLeds() {
  applyPattern({});
}

void applyPattern(const StatusPresentation::LedPattern &pattern) {
  setLedState(PinConfig::LED_ERROR, pattern.error);
  setLedState(PinConfig::LED_DATA, pattern.data);
  setLedState(PinConfig::LED_WARNING, pattern.warning);
  setLedState(PinConfig::LED_OK, pattern.ok);
}

} // namespace StatusLedController
