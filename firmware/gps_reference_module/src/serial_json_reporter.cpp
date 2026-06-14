#include "serial_json_reporter.h"

#include <Arduino.h>

#include "firmware_settings.h"

namespace SerialJsonReporter {

namespace {

void printJsonEscapedString(const char *value) {
  Serial.print(F("\""));

  if (value) {
    for (const char *p = value; *p; p++) {
      const unsigned char c = static_cast<unsigned char>(*p);

      switch (c) {
        case '\"': Serial.print(F("\\\"")); break;
        case '\\': Serial.print(F("\\\\")); break;
        case '\b': Serial.print(F("\\b")); break;
        case '\f': Serial.print(F("\\f")); break;
        case '\n': Serial.print(F("\\n")); break;
        case '\r': Serial.print(F("\\r")); break;
        case '\t': Serial.print(F("\\t")); break;

        default:
          if (c < 0x20) {
            char escaped[7];
            snprintf(escaped, sizeof(escaped), "\\u%04X", c);
            Serial.print(escaped);
          } else {
            Serial.write(c);
          }
          break;
      }
    }
  }

  Serial.print(F("\""));
}

void printJsonStringOrNull(bool valid, const char *value) {
  if (valid && value && value[0]) {
    printJsonEscapedString(value);
  } else {
    Serial.print(F("null"));
  }
}

void printJsonNumberOrNull(bool valid, double value, uint8_t decimals) {
  if (valid) {
    Serial.print(value, decimals);
  } else {
    Serial.print(F("null"));
  }
}

void printJsonAgeOrNull(bool valid, uint32_t ageMs) {
  if (valid) {
    Serial.print(static_cast<unsigned long>(ageMs));
  } else {
    Serial.print(F("null"));
  }
}

}  // namespace

void reportStartupJson(bool displayReady, uint8_t oledAddress) {
  if (!OutputConfig::EMIT_STARTUP_JSON) {
    return;
  }

  Serial.print(F("{\"type\":\"startup\",\"module\":"));
  printJsonEscapedString(Firmware::NAME);

  Serial.print(F(",\"version\":"));
  printJsonEscapedString(Firmware::VERSION);

  Serial.print(F(",\"usbBaud\":"));
  Serial.print(UsbConfig::BAUD_RATE);

  Serial.print(F(",\"gpsBaud\":"));
  Serial.print(GpsConfig::BAUD_RATE);

  Serial.print(F(",\"rawNmeaJson\":"));
  Serial.print(OutputConfig::EMIT_RAW_NMEA_JSON ? F("true") : F("false"));

  Serial.print(F(",\"oledAddress\":"));
  if (oledAddress != 0) {
    Serial.print(static_cast<unsigned int>(oledAddress));
  } else {
    Serial.print(F("null"));
  }

  Serial.print(F(",\"displayReady\":"));
  Serial.print(displayReady ? F("true") : F("false"));

  Serial.println(F("}"));
}

void reportRawNmeaJson(const char *sentence, bool checksumOk) {
  if (!OutputConfig::EMIT_RAW_NMEA_JSON) {
    return;
  }

  Serial.print(F("{\"type\":\"raw_nmea\",\"millis\":"));
  Serial.print(static_cast<unsigned long>(millis()));

  Serial.print(F(",\"checksumOk\":"));
  Serial.print(checksumOk ? F("true") : F("false"));

  Serial.print(F(",\"sentenceType\":"));
  printJsonEscapedString(
    GpsProcessing::sentenceTypeToText(GpsProcessing::detectSentenceTypeFromRaw(sentence))
  );

  Serial.print(F(",\"sentence\":"));
  printJsonEscapedString(sentence);

  Serial.println(F("}"));
}

void reportParsedStateJson(
  const GpsProcessing::GpsData &gps,
  const GpsProcessing::GpsValiditySnapshot &snapshot,
  bool displayReady
) {
  if (!OutputConfig::EMIT_PARSED_STATE_JSON) {
    return;
  }

  Serial.print(F("{\"type\":\"parsed_state\""));

  Serial.print(F(",\"millis\":"));
  Serial.print(static_cast<unsigned long>(snapshot.nowMs));

  Serial.print(F(",\"state\":"));
  printJsonEscapedString(GpsProcessing::diagnosticStateToJson(snapshot.diagnosticState));

  Serial.print(F(",\"valid\":"));
  Serial.print(snapshot.usablePosition ? F("true") : F("false"));

  Serial.print(F(",\"gpsData\":"));
  Serial.print(snapshot.freshNmea ? F("true") : F("false"));

  Serial.print(F(",\"displayReady\":"));
  Serial.print(displayReady ? F("true") : F("false"));

  Serial.print(F(",\"fix\":"));
  Serial.print(snapshot.currentFix ? F("true") : F("false"));

  Serial.print(F(",\"fixType\":"));
  printJsonEscapedString(GpsProcessing::fixTypeToText(gps, snapshot));

  Serial.print(F(",\"fixQuality\":"));
  Serial.print(snapshot.freshGga ? static_cast<unsigned int>(gps.fixQuality) : 0);

  Serial.print(F(",\"satellitesUsed\":"));
  Serial.print(snapshot.freshGga ? static_cast<unsigned int>(gps.satellitesUsed) : 0);

  Serial.print(F(",\"latitude\":"));
  printJsonNumberOrNull(snapshot.usablePosition, gps.latitude, 6);

  Serial.print(F(",\"longitude\":"));
  printJsonNumberOrNull(snapshot.usablePosition, gps.longitude, 6);

  Serial.print(F(",\"altitudeM\":"));
  printJsonNumberOrNull(snapshot.usableAltitude, gps.altitudeM, 1);

  Serial.print(F(",\"geoidSeparationM\":"));
  printJsonNumberOrNull(snapshot.freshGga && gps.geoidValid, gps.geoidSeparationM, 1);

  Serial.print(F(",\"hdop\":"));
  printJsonNumberOrNull(snapshot.usableHdop, gps.hdop, 1);

  Serial.print(F(",\"pdop\":"));
  printJsonNumberOrNull(snapshot.freshGsa && gps.pdopValid, gps.pdop, 1);

  Serial.print(F(",\"vdop\":"));
  printJsonNumberOrNull(snapshot.freshGsa && gps.vdopValid, gps.vdop, 1);

  Serial.print(F(",\"speedKnots\":"));
  printJsonNumberOrNull(snapshot.freshRmc && gps.speedValid, gps.speedKnots, 2);

  Serial.print(F(",\"speedKmh\":"));
  printJsonNumberOrNull(snapshot.freshRmc && gps.speedValid, gps.speedKmh, 2);

  Serial.print(F(",\"courseDeg\":"));
  printJsonNumberOrNull(snapshot.freshRmc && gps.courseValid, gps.courseDeg, 1);

  Serial.print(F(",\"utcTime\":"));
  printJsonStringOrNull((snapshot.freshGga || snapshot.freshRmc) && gps.timeValid, gps.utcTime);

  Serial.print(F(",\"utcDate\":"));
  printJsonStringOrNull(snapshot.freshRmc && gps.dateValid, gps.utcDate);

  Serial.print(F(",\"nmeaAgeMs\":"));
  printJsonAgeOrNull(snapshot.freshNmea, snapshot.nmeaAgeMs);

  Serial.print(F(",\"ggaAgeMs\":"));
  printJsonAgeOrNull(snapshot.freshGga, snapshot.ggaAgeMs);

  Serial.print(F(",\"gsaAgeMs\":"));
  printJsonAgeOrNull(snapshot.freshGsa, snapshot.gsaAgeMs);

  Serial.print(F(",\"rmcAgeMs\":"));
  printJsonAgeOrNull(snapshot.freshRmc, snapshot.rmcAgeMs);

  Serial.print(F(",\"rawSentenceCount\":"));
  Serial.print(gps.rawSentenceCount);

  Serial.print(F(",\"acceptedSentenceCount\":"));
  Serial.print(gps.acceptedSentenceCount);

  Serial.print(F(",\"checksumErrorCount\":"));
  Serial.print(gps.checksumErrorCount);

  Serial.print(F(",\"bufferOverflowCount\":"));
  Serial.print(gps.bufferOverflowCount);

  Serial.println(F("}"));
}

}  // namespace SerialJsonReporter
