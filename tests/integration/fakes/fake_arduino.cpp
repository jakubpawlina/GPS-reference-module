#include "Arduino.h"

#include <cstring>
#include <iomanip>
#include <map>
#include <sstream>

#include "Adafruit_SSD1306.h"
#include "Wire.h"

namespace {

uint32_t currentMillis;
std::map<uint8_t, FakeArduino::PinState> pins;
std::map<int, HardwareSerial *> hardwareSerials;
Adafruit_SSD1306 *displayInstance;

}  // namespace

FakeArduino::SerialPort Serial;
TwoWire Wire;

namespace FakeArduino {

void reset() {
  currentMillis = 0;
  pins.clear();
  Serial.clearOutput();
  Wire.reset();
}

void setMillis(uint32_t value) {
  currentMillis = value;
}

uint32_t now() {
  return currentMillis;
}

PinState pinState(uint8_t pin) {
  return pins[pin];
}

void SerialPort::begin(uint32_t baud) {
  baud_ = baud;
}

void SerialPort::print(const char *value) {
  if (value) output_ += value;
}

void SerialPort::print(char value) {
  output_ += value;
}

void SerialPort::print(unsigned char value) {
  output_ += std::to_string(static_cast<unsigned int>(value));
}

void SerialPort::print(unsigned char value, int base) {
  if (base == HEX) {
    std::ostringstream stream;
    stream << std::uppercase << std::hex << static_cast<unsigned int>(value);
    output_ += stream.str();
  } else {
    print(value);
  }
}

void SerialPort::print(unsigned int value) {
  output_ += std::to_string(value);
}

void SerialPort::print(unsigned long value) {
  output_ += std::to_string(value);
}

void SerialPort::print(double value, uint8_t decimals) {
  std::ostringstream stream;
  stream << std::fixed << std::setprecision(decimals) << value;
  output_ += stream.str();
}

void SerialPort::println(const char *value) {
  print(value);
  output_ += '\n';
}

void SerialPort::println() {
  output_ += '\n';
}

size_t SerialPort::write(unsigned char value) {
  output_ += static_cast<char>(value);
  return 1;
}

uint32_t SerialPort::baud() const {
  return baud_;
}

const std::string &SerialPort::output() const {
  return output_;
}

void SerialPort::clearOutput() {
  output_.clear();
}

HardwareSerial *hardwareSerial(int id) {
  const auto found = hardwareSerials.find(id);
  return found == hardwareSerials.end() ? nullptr : found->second;
}

}  // namespace FakeArduino

HardwareSerial::HardwareSerial(int id) : id_(id) {
  hardwareSerials[id] = this;
}

void HardwareSerial::begin(uint32_t baud, uint32_t, int8_t rxPin, int8_t txPin) {
  baud_ = baud;
  rxPin_ = rxPin;
  txPin_ = txPin;
}

int HardwareSerial::available() const {
  return static_cast<int>(input_.size() - readPosition_);
}

int HardwareSerial::read() {
  if (readPosition_ >= input_.size()) return -1;
  return static_cast<unsigned char>(input_[readPosition_++]);
}

void HardwareSerial::inject(const std::string &data) {
  if (readPosition_ == input_.size()) {
    input_.clear();
    readPosition_ = 0;
  }
  input_ += data;
}

uint32_t HardwareSerial::baud() const {
  return baud_;
}

int8_t HardwareSerial::rxPin() const {
  return rxPin_;
}

int8_t HardwareSerial::txPin() const {
  return txPin_;
}

uint32_t millis() {
  return FakeArduino::now();
}

void delay(uint32_t milliseconds) {
  FakeArduino::setMillis(FakeArduino::now() + milliseconds);
}

void pinMode(uint8_t pin, uint8_t mode) {
  pins[pin].mode = mode;
}

void digitalWrite(uint8_t pin, uint8_t value) {
  pins[pin].value = value;
}

void TwoWire::end() {}

void TwoWire::begin(uint8_t sda, uint8_t scl) {
  sda_ = sda;
  scl_ = scl;
}

void TwoWire::setClock(uint32_t clock) {
  clock_ = clock;
}

void TwoWire::beginTransmission(uint8_t address) {
  transmissionAddress_ = address;
}

uint8_t TwoWire::endTransmission() {
  return transmissionAddress_ == presentAddress_ ? 0 : 4;
}

void TwoWire::reset() {
  sda_ = 0;
  scl_ = 0;
  transmissionAddress_ = 0;
  presentAddress_ = 0x3C;
  clock_ = 0;
}

void TwoWire::setPresentAddress(uint8_t address) {
  presentAddress_ = address;
}

uint8_t TwoWire::sda() const {
  return sda_;
}

uint8_t TwoWire::scl() const {
  return scl_;
}

uint32_t TwoWire::clock() const {
  return clock_;
}

Adafruit_SSD1306::Adafruit_SSD1306(uint8_t, uint8_t, TwoWire *, int8_t) {
  displayInstance = this;
}

bool Adafruit_SSD1306::begin(uint8_t, uint8_t address, bool, bool) {
  address_ = address;
  return true;
}

void Adafruit_SSD1306::clearDisplay() {
  for (std::string &row : rows_) row.clear();
}

void Adafruit_SSD1306::setTextSize(uint8_t) {}

void Adafruit_SSD1306::setTextColor(uint8_t) {}

void Adafruit_SSD1306::setCursor(int16_t x, int16_t y) {
  cursorX_ = x;
  cursorY_ = y;
}

void Adafruit_SSD1306::print(const char *value) {
  const size_t rowIndex = static_cast<size_t>(cursorY_ / 8);
  if (rowIndex >= rows_.size() || !value) return;

  std::string &row = rows_[rowIndex];
  const size_t characterPosition = static_cast<size_t>(cursorX_ / 6);
  if (row.size() < characterPosition) row.resize(characterPosition, ' ');
  if (row.size() < characterPosition + strlen(value)) {
    row.resize(characterPosition + strlen(value), ' ');
  }
  row.replace(characterPosition, strlen(value), value);
  cursorX_ += static_cast<int16_t>(strlen(value) * 6);
}

void Adafruit_SSD1306::display() {
  displayCount_++;
}

const std::string &Adafruit_SSD1306::row(uint8_t row) const {
  return rows_.at(row);
}

uint8_t Adafruit_SSD1306::address() const {
  return address_;
}

unsigned int Adafruit_SSD1306::displayCount() const {
  return displayCount_;
}

namespace FakeDisplay {

Adafruit_SSD1306 *instance() {
  return displayInstance;
}

}  // namespace FakeDisplay
