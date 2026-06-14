#include <cstdlib>
#include <iostream>
#include <string>
#include <string_view>

#include "Adafruit_SSD1306.h"
#include "Arduino.h"
#include "Wire.h"

#include "../../firmware/gps_reference_module/src/firmware_runtime.h"
#include "../../firmware/gps_reference_module/src/firmware_settings.h"

namespace {

struct Scenario {
  const char *name;
  const char *nmea;
  const char *jsonState;
  const char *displayState;
  bool errorLed;
  bool dataLed;
  bool warningLed;
  bool okLed;
};

const Scenario scenarios[] = {
    /*
     * No receiver traffic after boot.
     * Expects an error state, no data indication, and placeholder OLED fields.
     */
    {
        "no-data",
        "",
        "NO_GPS_DATA",
        "NO DATA",
        true,
        false,
        false,
        false,
    },
    /*
     * Valid NMEA transport without a position fix.
     * Expects both data and error indications while position fields remain unusable.
     */
    {
        "no-fix",
        "$GNGGA,010101.00,,,,,0,03,99.9,0.0,M,34.0,M,,*74\r\n"
        "$GNRMC,010101.00,V,,,,,0.02,054.7,260426,,,A*5D\r\n"
        "$GNGSA,A,1,,,,,,,,,,,,,99.9,99.9,99.9*17\r\n",
        "NO_FIX",
        "NO FIX",
        true,
        true,
        false,
        false,
    },
    /*
     * Valid position with a two-dimensional fix.
     * Expects warning presentation because altitude cannot be trusted.
     */
    {
        "fix-2d",
        "$GNGGA,010101.00,5213.7820,N,02100.7320,E,1,04,1.7,120.0,M,34.0,M,,*78\r\n"
        "$GNRMC,010101.00,A,5213.7820,N,02100.7320,E,0.02,054.7,260426,,,A*7C\r\n"
        "$GNGSA,A,2,10,11,12,13,,,,,,,,,2.8,1.7,9.9*21\r\n",
        "DEGRADED_2D",
        "WARN 2D",
        false,
        true,
        true,
        false,
    },
    /*
     * Valid three-dimensional fix below the configured satellite threshold.
     * Expects usable position data with a degraded low-satellite warning.
     */
    {
        "low-sat",
        "$GNGGA,010101.00,5213.7820,N,02100.7320,E,1,05,0.8,120.0,M,34.0,M,,*77\r\n"
        "$GNRMC,010101.00,A,5213.7820,N,02100.7320,E,0.02,054.7,260426,,,A*7C\r\n"
        "$GNGSA,A,3,10,11,12,13,14,,,,,,,,1.6,0.8,1.4*23\r\n",
        "DEGRADED_LOW_SAT",
        "LOW SAT",
        false,
        true,
        true,
        false,
    },
    /*
     * Healthy three-dimensional reference fix.
     * Expects valid position/altitude data and the green OK indication.
     */
    {
        "ok",
        "$GNGGA,010101.00,5213.7820,N,02100.7320,E,1,09,0.8,120.0,M,34.0,M,,*7B\r\n"
        "$GNRMC,010101.00,A,5213.7820,N,02100.7320,E,0.02,054.7,260426,,,A*7C\r\n"
        "$GNGSA,A,3,10,11,12,13,14,15,16,17,18,,,,1.6,0.8,1.4*2F\r\n",
        "REFERENCE_OK",
        "OK",
        false,
        true,
        false,
        true,
    },
};

void require(bool condition, const std::string &message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

const Scenario &findScenario(std::string_view name) {
  for (const Scenario &scenario : scenarios) {
    if (name == scenario.name)
      return scenario;
  }
  std::cerr << "Unknown scenario: " << name << '\n';
  std::exit(2);
}

void requireLed(uint8_t pin, bool expectedOn, const char *name) {
  const FakeArduino::PinState state = FakeArduino::pinState(pin);
  if (expectedOn) {
    require(state.mode == OUTPUT && state.value == HIGH, std::string(name) + " LED should be on");
  } else if (pin == PinConfig::LED_ERROR || pin == PinConfig::LED_DATA) {
    require(state.mode == INPUT_PULLDOWN, std::string(name) + " LED should use pulldown off state");
  } else {
    require(state.mode == OUTPUT && state.value == LOW, std::string(name) + " LED should be off");
  }
}

} // namespace

/**
 * Purpose: Exercise the real firmware setup/loop orchestration as one host process.
 * Setup: Select one GPS scenario, initialize fake UART/I2C/OLED/GPIO/time peripherals,
 *        inject its NMEA frame, advance time, and execute one firmware loop.
 * Verifies: Hardware configuration, startup and parsed JSON, sentence counts, OLED state,
 *           and all four LED outputs agree for the selected scenario.
 *
 * The runner launches this executable once per scenario so firmware globals begin from
 * the same reset state they would have after booting physical hardware.
 */
int main(int argc, char **argv) {
  require(argc == 2, "usage: test_firmware_integration <scenario>");
  const Scenario &scenario = findScenario(argv[1]);

  FakeArduino::reset();
  Wire.setPresentAddress(DisplayConfig::I2C_ADDRESS);
  setupFirmware();

  require(Serial.baud() == UsbConfig::BAUD_RATE, "USB serial baud mismatch");
  HardwareSerial *gps = FakeArduino::hardwareSerial(2);
  require(gps != nullptr, "GPS hardware serial was not created");
  require(gps->baud() == GpsConfig::BAUD_RATE, "GPS serial baud mismatch");
  require(gps->rxPin() == PinConfig::GPS_RX, "GPS RX pin mismatch");
  require(gps->txPin() == PinConfig::GPS_TX, "GPS TX pin mismatch");
  require(Wire.sda() == PinConfig::OLED_SDA && Wire.scl() == PinConfig::OLED_SCL,
          "OLED I2C pins mismatch");
  require(Wire.clock() == 100000, "OLED I2C clock mismatch");

  require(Serial.output().find("\"type\":\"startup\"") != std::string::npos,
          "startup JSON missing");
  require(Serial.output().find("\"oledAddress\":60") != std::string::npos,
          "OLED address JSON should be decimal");
  require(Serial.output().find("\"displayReady\":true") != std::string::npos,
          "display readiness missing");

  gps->inject(scenario.nmea);
  FakeArduino::setMillis(1000);
  loopFirmware();

  const std::string output = Serial.output();
  require(output.find(std::string("\"state\":\"") + scenario.jsonState + "\"") != std::string::npos,
          std::string("parsed JSON state mismatch for ") + scenario.name);

  const size_t expectedRawCount = scenario.nmea[0] == '\0' ? 0 : 3;
  require(output.find(std::string("\"rawSentenceCount\":") + std::to_string(expectedRawCount)) !=
              std::string::npos,
          std::string("raw sentence count mismatch for ") + scenario.name);

  Adafruit_SSD1306 *display = FakeDisplay::instance();
  require(display != nullptr, "OLED instance missing");
  require(display->address() == DisplayConfig::I2C_ADDRESS, "OLED address mismatch");
  require(display->displayCount() >= 2, "OLED should render boot and runtime screens");
  require(display->row(0).find(scenario.displayState) != std::string::npos,
          std::string("display state mismatch for ") + scenario.name + ": " + display->row(0));

  requireLed(PinConfig::LED_ERROR, scenario.errorLed, "error");
  requireLed(PinConfig::LED_DATA, scenario.dataLed, "data");
  requireLed(PinConfig::LED_WARNING, scenario.warningLed, "warning");
  requireLed(PinConfig::LED_OK, scenario.okLed, "ok");

  std::cout << "Integration scenario passed: " << scenario.name << '\n';
  return 0;
}
