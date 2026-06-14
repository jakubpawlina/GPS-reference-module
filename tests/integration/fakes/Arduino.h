#pragma once

#include <stddef.h>
#include <stdint.h>
#include <stdio.h>

#include <string>

#define F(value) value

constexpr uint8_t LOW = 0;
constexpr uint8_t HIGH = 1;
constexpr uint8_t INPUT = 0;
constexpr uint8_t OUTPUT = 1;
constexpr uint8_t INPUT_PULLUP = 2;
constexpr uint8_t INPUT_PULLDOWN = 3;
constexpr uint32_t SERIAL_8N1 = 0;
constexpr int HEX = 16;

namespace FakeArduino {

struct PinState {
  uint8_t mode = INPUT;
  uint8_t value = LOW;
};

void reset();
void setMillis(uint32_t value);
uint32_t now();
PinState pinState(uint8_t pin);

class SerialPort {
 public:
  void begin(uint32_t baud);
  void print(const char *value);
  void print(char value);
  void print(unsigned char value);
  void print(unsigned char value, int base);
  void print(unsigned int value);
  void print(unsigned long value);
  void print(double value, uint8_t decimals);
  void println(const char *value);
  void println();
  size_t write(unsigned char value);

  uint32_t baud() const;
  const std::string &output() const;
  void clearOutput();

 private:
  uint32_t baud_ = 0;
  std::string output_;
};

}  // namespace FakeArduino

class HardwareSerial {
 public:
  explicit HardwareSerial(int id);

  void begin(uint32_t baud, uint32_t config, int8_t rxPin, int8_t txPin);
  int available() const;
  int read();
  void inject(const std::string &data);

  uint32_t baud() const;
  int8_t rxPin() const;
  int8_t txPin() const;

 private:
  int id_;
  uint32_t baud_ = 0;
  int8_t rxPin_ = -1;
  int8_t txPin_ = -1;
  std::string input_;
  size_t readPosition_ = 0;
};

namespace FakeArduino {
HardwareSerial *hardwareSerial(int id);
}

extern FakeArduino::SerialPort Serial;

uint32_t millis();
void delay(uint32_t milliseconds);
void pinMode(uint8_t pin, uint8_t mode);
void digitalWrite(uint8_t pin, uint8_t value);
