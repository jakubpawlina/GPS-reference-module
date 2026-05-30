#pragma once

#include <stddef.h>
#include <stdint.h>

/** @brief Immutable build identity for the firmware image. */
namespace Firmware {
  constexpr const char *NAME = "gps-reference-module";
  constexpr const char *VERSION = "1.2.0";
}

/** @brief SSD1306 display geometry and supported I2C addresses. */
namespace DisplayConfig {
  constexpr uint8_t WIDTH = 128;
  constexpr uint8_t HEIGHT = 64;
  constexpr int8_t RESET_PIN = -1;
  constexpr uint8_t I2C_ADDRESS = 0x3C;
  constexpr uint8_t I2C_ADDRESS_ALT = 0x3D;
}

/** @brief Physical GPIO mapping for the deployed ESP32 wiring. */
namespace PinConfig {
  constexpr uint8_t GPS_RX = 16;
  constexpr uint8_t GPS_TX = 17;

  constexpr uint8_t OLED_SDA = 19;
  constexpr uint8_t OLED_SCL = 22;

  constexpr uint8_t LED_ERROR   = 23;
  constexpr uint8_t LED_DATA    = 21;
  constexpr uint8_t LED_WARNING = 5;
  constexpr uint8_t LED_OK      = 25;
}

/** @brief Runtime timing constants in milliseconds. */
namespace TimingConfig {
  constexpr uint32_t GPS_DATA_TIMEOUT_MS = 1800;
  constexpr uint32_t DISPLAY_REFRESH_MS = 1000;
  constexpr uint32_t PARSED_REPORT_INTERVAL_MS = 1000;
  constexpr uint32_t LOOP_DELAY_MS = 5;
}

/** @brief GPS receiver requirements and acceptance thresholds. */
namespace GpsConfig {
  constexpr uint32_t BAUD_RATE = 9600;
  constexpr uint8_t MIN_OK_SATELLITES = 6;
}

/** @brief USB serial configuration for downstream consumers. */
namespace UsbConfig {
  constexpr uint32_t BAUD_RATE = 115200;
}

/** @brief NMEA sentence handling configuration. */
namespace NmeaConfig {
  constexpr size_t BUFFER_SIZE = 144;
  constexpr bool REQUIRE_CHECKSUM = false;
}

/** @brief Output stream feature flags. */
namespace OutputConfig {
  constexpr bool EMIT_STARTUP_JSON = true;
  constexpr bool EMIT_RAW_NMEA_JSON = true;
  constexpr bool EMIT_PARSED_STATE_JSON = true;
}
