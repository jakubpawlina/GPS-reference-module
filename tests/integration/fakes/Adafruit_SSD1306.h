#pragma once

#include <stdint.h>

#include <array>
#include <string>

class TwoWire;

constexpr uint8_t SSD1306_WHITE = 1;
constexpr uint8_t SSD1306_SWITCHCAPVCC = 2;

class Adafruit_SSD1306 {
public:
  Adafruit_SSD1306(uint8_t width, uint8_t height, TwoWire *wire, int8_t resetPin);

  bool begin(uint8_t powerMode, uint8_t address, bool reset, bool periphBegin);
  void clearDisplay();
  void setTextSize(uint8_t size);
  void setTextColor(uint8_t color);
  void setCursor(int16_t x, int16_t y);
  void print(const char *value);
  void display();

  const std::string &row(uint8_t row) const;
  uint8_t address() const;
  unsigned int displayCount() const;

private:
  uint8_t address_ = 0;
  int16_t cursorX_ = 0;
  int16_t cursorY_ = 0;
  unsigned int displayCount_ = 0;
  std::array<std::string, 8> rows_;
};

namespace FakeDisplay {
Adafruit_SSD1306 *instance();
}
