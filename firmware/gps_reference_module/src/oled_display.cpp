#include "oled_display.h"

#include <Arduino.h>
#include <Wire.h>

#include "firmware_settings.h"

namespace OledDisplay {

namespace {

void drawDisplayRow(Adafruit_SSD1306 &display, uint8_t row, const char *label, const char *value) {
  constexpr int labelX = 0;
  constexpr int colonX = 36;
  constexpr int valueX = 46;
  constexpr int rowHeight = 8;

  const int y = static_cast<int>(row) * rowHeight;

  display.setCursor(labelX, y);
  display.print(label);

  display.setCursor(colonX, y);
  display.print(":");

  display.setCursor(valueX, y);
  display.print(value);
}

void renderBootScreen(Adafruit_SSD1306 &display) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  drawDisplayRow(display, 0, "STATE", "START");
  drawDisplayRow(display, 1, "FIX", "---");
  drawDisplayRow(display, 2, "SATS", "---");
  drawDisplayRow(display, 3, "LAT", "---");
  drawDisplayRow(display, 4, "LON", "---");
  drawDisplayRow(display, 5, "ALT", "---");
  drawDisplayRow(display, 6, "HDOP", "---");
  drawDisplayRow(display, 7, "AGE", "---");

  display.display();
}

} // namespace

uint8_t probeOledAddress() {
  Wire.end();
  pinMode(PinConfig::OLED_SDA, INPUT);
  pinMode(PinConfig::OLED_SCL, INPUT);
  delay(5);

  Wire.begin(PinConfig::OLED_SDA, PinConfig::OLED_SCL);
  Wire.setClock(100000);
  delay(10);

  const uint8_t candidates[] = {DisplayConfig::I2C_ADDRESS, DisplayConfig::I2C_ADDRESS_ALT};

  for (uint8_t i = 0; i < sizeof(candidates); i++) {
    Wire.beginTransmission(candidates[i]);
    if (Wire.endTransmission() == 0) {
      return candidates[i];
    }
  }

  return 0;
}

bool initializeDisplay(Adafruit_SSD1306 &display, uint8_t &detectedAddress) {
  detectedAddress = probeOledAddress();
  if (detectedAddress == 0) {
    return false;
  }

  if (!display.begin(SSD1306_SWITCHCAPVCC, detectedAddress, false, false)) {
    return false;
  }

  renderBootScreen(display);
  return true;
}

void renderDisplay(Adafruit_SSD1306 &display, const StatusPresentation::DisplayModel &model) {
  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  drawDisplayRow(display, 0, "STATE", model.state);
  drawDisplayRow(display, 1, "FIX", model.fix);
  drawDisplayRow(display, 2, "SATS", model.sats);
  drawDisplayRow(display, 3, "LAT", model.latitude);
  drawDisplayRow(display, 4, "LON", model.longitude);
  drawDisplayRow(display, 5, "ALT", model.altitude);
  drawDisplayRow(display, 6, "HDOP", model.hdop);
  drawDisplayRow(display, 7, "AGE", model.age);

  display.display();
}

} // namespace OledDisplay
