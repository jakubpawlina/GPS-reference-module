#pragma once

#include <stdint.h>

class TwoWire {
public:
  void end();
  void begin(uint8_t sda, uint8_t scl);
  void setClock(uint32_t clock);
  void beginTransmission(uint8_t address);
  uint8_t endTransmission();

  void reset();
  void setPresentAddress(uint8_t address);

  uint8_t sda() const;
  uint8_t scl() const;
  uint32_t clock() const;

private:
  uint8_t sda_ = 0;
  uint8_t scl_ = 0;
  uint8_t transmissionAddress_ = 0;
  uint8_t presentAddress_ = 0x3C;
  uint32_t clock_ = 0;
};

extern TwoWire Wire;
