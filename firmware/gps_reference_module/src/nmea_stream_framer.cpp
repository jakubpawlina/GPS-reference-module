#include "nmea_stream_framer.h"

#include <string.h>

namespace NmeaStreamFramer {

LineAccumulator::LineAccumulator() : buffer_{0}, position_(0) {}

void LineAccumulator::reset() {
  position_ = 0;
  buffer_[0] = '\0';
}

FeedResult LineAccumulator::feed(char c, char *completedSentence, size_t completedSentenceSize) {
  if (c == '\r') {
    return FeedResult::None;
  }

  if (c == '$') {
    reset();
    buffer_[position_++] = c;
    return FeedResult::None;
  }

  if (c == '\n') {
    if (position_ > 0 && buffer_[0] == '$' && completedSentence && completedSentenceSize > 0) {
      buffer_[position_] = '\0';
      strncpy(completedSentence, buffer_, completedSentenceSize - 1);
      completedSentence[completedSentenceSize - 1] = '\0';
      reset();
      return FeedResult::Complete;
    }

    reset();
    return FeedResult::None;
  }

  if (position_ == 0) {
    return FeedResult::None;
  }

  if (position_ < sizeof(buffer_) - 1) {
    buffer_[position_++] = c;
    return FeedResult::None;
  }

  reset();
  return FeedResult::Overflow;
}

} // namespace NmeaStreamFramer
