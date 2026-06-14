#pragma once

#include <stddef.h>
#include <stdint.h>

#include "firmware_settings.h"

/**
 * @brief Byte-stream to line-oriented NMEA framing.
 *
 * This is intentionally independent of HardwareSerial so overflow and framing
 * behavior can be validated on the host.
 */
namespace NmeaStreamFramer {

/** @brief Outcome of feeding one byte into the line accumulator. */
enum class FeedResult : uint8_t { None, Complete, Overflow };

/** @brief Stateful accumulator that extracts NMEA sentences from a byte stream. */
class LineAccumulator {
public:
  LineAccumulator();

  /** @brief Clear any buffered partial sentence state. */
  void reset();
  /** @brief Feed one byte of input into the accumulator. */
  FeedResult feed(char c, char *completedSentence, size_t completedSentenceSize);

private:
  char buffer_[NmeaConfig::BUFFER_SIZE];
  size_t position_;
};

} // namespace NmeaStreamFramer
