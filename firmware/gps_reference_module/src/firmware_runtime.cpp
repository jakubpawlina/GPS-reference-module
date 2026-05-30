#include "firmware_runtime.h"

#include <Arduino.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <Wire.h>

#include "firmware_settings.h"
#include "gps_processing.h"
#include "nmea_stream_framer.h"
#include "oled_display.h"
#include "serial_json_reporter.h"
#include "status_led_controller.h"
#include "status_presentation.h"

namespace {

struct FirmwareState {
  GpsProcessing::GpsData gps;
  NmeaStreamFramer::LineAccumulator framer;
  char nmeaSentence[144] = "";
  bool displayReady = false;
  uint8_t detectedOledAddress = 0;
  uint32_t lastDisplayUpdateMs = 0;
  uint32_t lastParsedJsonReportMs = 0;
};

HardwareSerial gpsSerial(2);

Adafruit_SSD1306 display(
  DisplayConfig::WIDTH,
  DisplayConfig::HEIGHT,
  &Wire,
  DisplayConfig::RESET_PIN
);

FirmwareState state;

GpsProcessing::GpsValiditySnapshot buildGpsSnapshot(uint32_t nowMs) {
  return GpsProcessing::buildGpsSnapshot(
    state.gps,
    nowMs,
    TimingConfig::GPS_DATA_TIMEOUT_MS,
    GpsConfig::MIN_OK_SATELLITES
  );
}

void processNmeaSentence(const char *sentence) {
  const bool checksumOk = GpsProcessing::verifyNmeaChecksum(sentence, NmeaConfig::REQUIRE_CHECKSUM);
  SerialJsonReporter::reportRawNmeaJson(sentence, checksumOk);
  GpsProcessing::processNmeaSentence(state.gps, sentence, millis(), NmeaConfig::REQUIRE_CHECKSUM);
}

void readGpsSerial() {
  while (gpsSerial.available()) {
    const char c = static_cast<char>(gpsSerial.read());

    switch (state.framer.feed(c, state.nmeaSentence, sizeof(state.nmeaSentence))) {
      case NmeaStreamFramer::FeedResult::Complete:
        processNmeaSentence(state.nmeaSentence);
        break;
      case NmeaStreamFramer::FeedResult::Overflow:
        state.gps.bufferOverflowCount++;
        break;
      case NmeaStreamFramer::FeedResult::None:
        break;
    }
  }
}

void initializeDisplay() {
  state.displayReady = OledDisplay::initializeDisplay(display, state.detectedOledAddress);

  if (!state.displayReady) {
    if (state.detectedOledAddress == 0) {
      Serial.println(F("{\"type\":\"error\",\"msg\":\"OLED not found on I2C - check SDA/SCL/VCC wiring\"}"));
    } else {
      Serial.println(F("{\"type\":\"error\",\"msg\":\"OLED begin() failed (out of memory)\"}"));
    }
  }
}

void updateDisplay(const GpsProcessing::GpsValiditySnapshot &snapshot) {
  if (!state.displayReady) {
    return;
  }

  StatusPresentation::DisplayModel model;
  StatusPresentation::buildDisplayModel(state.gps, snapshot, model);
  OledDisplay::renderDisplay(display, model);
}

void updateLeds(const GpsProcessing::GpsValiditySnapshot &snapshot) {
  StatusLedController::applyPattern(StatusPresentation::buildLedPattern(snapshot));
}

}  // namespace

void setupFirmware() {
  Serial.begin(UsbConfig::BAUD_RATE);
  delay(500);

  initializeDisplay();
  StatusLedController::initializeLeds();

  gpsSerial.begin(GpsConfig::BAUD_RATE, SERIAL_8N1, PinConfig::GPS_RX, PinConfig::GPS_TX);
  SerialJsonReporter::reportStartupJson(state.displayReady, state.detectedOledAddress);
}

void loopFirmware() {
  readGpsSerial();

  const uint32_t nowMs = millis();
  const GpsProcessing::GpsValiditySnapshot snapshot = buildGpsSnapshot(nowMs);

  updateLeds(snapshot);

  if (nowMs - state.lastDisplayUpdateMs >= TimingConfig::DISPLAY_REFRESH_MS) {
    state.lastDisplayUpdateMs = nowMs;
    updateDisplay(snapshot);
  }

  if (nowMs - state.lastParsedJsonReportMs >= TimingConfig::PARSED_REPORT_INTERVAL_MS) {
    state.lastParsedJsonReportMs = nowMs;
    SerialJsonReporter::reportParsedStateJson(state.gps, snapshot, state.displayReady);
  }

  delay(TimingConfig::LOOP_DELAY_MS);
}
