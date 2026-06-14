#pragma once

#include <stddef.h>
#include <stdint.h>

/** @brief Immutable build identity for the firmware image. */
namespace Firmware {
constexpr const char *NAME = "gps-reference-module";
constexpr const char *VERSION = "1.2.0";
} // namespace Firmware

/** @brief SSD1306 display geometry and supported I2C addresses. */
namespace DisplayConfig {
constexpr uint8_t WIDTH = 128;
constexpr uint8_t HEIGHT = 64;
constexpr int8_t RESET_PIN = -1;
constexpr uint8_t I2C_ADDRESS = 0x3C;
constexpr uint8_t I2C_ADDRESS_ALT = 0x3D;
} // namespace DisplayConfig

/** @brief Physical GPIO mapping for the deployed ESP32 wiring. */
namespace PinConfig {
constexpr uint8_t GPS_RX = 16;
constexpr uint8_t GPS_TX = 17;

constexpr uint8_t OLED_SDA = 19;
constexpr uint8_t OLED_SCL = 22;

constexpr uint8_t LED_ERROR = 23;
constexpr uint8_t LED_DATA = 21;
constexpr uint8_t LED_WARNING = 5;
constexpr uint8_t LED_OK = 25;
} // namespace PinConfig

/** @brief Runtime timing constants in milliseconds. */
namespace TimingConfig {
/**
 * A GPS sentence timestamp is considered fresh if it arrived within this
 * window.  Standard receivers output at 1 Hz; 1800 ms (1.8x the period)
 * tolerates exactly one missed sentence before declaring data loss.
 */
constexpr uint32_t GPS_DATA_TIMEOUT_MS = 1800;

/**
 * How often the OLED is redrawn.  Matches the 1 Hz GPS update rate so the
 * display never shows content that is more than one epoch stale.
 */
constexpr uint32_t DISPLAY_REFRESH_MS = 1000;

/**
 * How often a parsed_state JSON record is emitted over USB.  At 1 Hz the
 * record is ~500 bytes, consuming less than 5% of the 115200-baud budget.
 */
constexpr uint32_t PARSED_REPORT_INTERVAL_MS = 1000;

/**
 * Main-loop sleep between iterations.  200 Hz is fast enough to drain the
 * GPS UART FIFO (9600 baud, ~1 char/ms) well before the next NMEA sentence
 * arrives, while leaving the processor mostly idle.
 */
constexpr uint32_t LOOP_DELAY_MS = 5;
} // namespace TimingConfig

/** @brief GPS receiver requirements and acceptance thresholds. */
namespace GpsConfig {
/** Default NEO-6M output baud rate; change to match receiver configuration. */
constexpr uint32_t BAUD_RATE = 9600;

/**
 * Minimum satellites required for the REFERENCE_OK state.  Six satellites
 * provide one redundant SV beyond the minimum four needed for a 3D fix,
 * giving the receiver enough geometry to detect and reject a faulty SV.
 */
constexpr uint8_t MIN_OK_SATELLITES = 6;
} // namespace GpsConfig

/** @brief USB serial configuration for downstream consumers. */
namespace UsbConfig {
/** Must match the baud rate used by the Raspberry Pi reader service. */
constexpr uint32_t BAUD_RATE = 115200;
} // namespace UsbConfig

/** @brief NMEA sentence handling configuration. */
namespace NmeaConfig {
/**
 * Maximum raw sentence length including the leading '$' but excluding the
 * trailing CR/LF.  NMEA 0183 limits standard sentences to 82 characters;
 * 144 bytes accommodates extended proprietary sentences with ample margin.
 */
constexpr size_t BUFFER_SIZE = 144;

/**
 * When false, sentences without a '*XX' checksum field are accepted.
 * Useful during bench testing with a terminal emulator.  Set true for
 * production deployments to reject noise from an electrically noisy link.
 */
constexpr bool REQUIRE_CHECKSUM = false;
} // namespace NmeaConfig

/** @brief Output stream feature flags. */
namespace OutputConfig {
constexpr bool EMIT_STARTUP_JSON = true;
constexpr bool EMIT_RAW_NMEA_JSON = true;
constexpr bool EMIT_PARSED_STATE_JSON = true;
} // namespace OutputConfig
