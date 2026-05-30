#include <Arduino.h>
#include <Wire.h>
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include <ctype.h>
#include <stdlib.h>
#include <string.h>

namespace Firmware {
  constexpr const char *NAME = "gps-reference-module";
  constexpr const char *VERSION = "1.2.0";
}

namespace DisplayConfig {
  constexpr uint8_t WIDTH = 128;
  constexpr uint8_t HEIGHT = 64;
  constexpr int8_t RESET_PIN = -1;
  constexpr uint8_t I2C_ADDRESS = 0x3C;
  constexpr uint8_t I2C_ADDRESS_ALT = 0x3D;
}

namespace PinConfig {
  constexpr uint8_t GPS_RX = 16;
  constexpr uint8_t GPS_TX = 17;

  constexpr uint8_t OLED_SDA = 19;
  constexpr uint8_t OLED_SCL = 22;

  constexpr uint8_t LED_ERROR   = 23;  // Red
  constexpr uint8_t LED_DATA    = 21;  // Blue
  constexpr uint8_t LED_WARNING =  5;  // Yellow  (GPIO5 is adjacent to GPS_TX/GPIO17 – see hardware.md)
  constexpr uint8_t LED_OK      = 25;  // Green   (GPIO25, right side, clear of I2C/UART pins)
}

namespace TimingConfig {
  constexpr uint32_t GPS_DATA_TIMEOUT_MS = 1800;
  constexpr uint32_t DISPLAY_REFRESH_MS = 1000;
  constexpr uint32_t PARSED_REPORT_INTERVAL_MS = 1000;
  constexpr uint32_t LOOP_DELAY_MS = 5;
}

namespace GpsConfig {
  constexpr uint32_t BAUD_RATE = 9600;
  constexpr uint8_t MIN_OK_SATELLITES = 6;
}

namespace UsbConfig {
  constexpr uint32_t BAUD_RATE = 115200;
}

namespace NmeaConfig {
  constexpr size_t BUFFER_SIZE = 144;
  constexpr size_t MAX_FIELDS = 24;

  constexpr bool REQUIRE_CHECKSUM = false;  // set true to reject sentences without a checksum
}

namespace OutputConfig {
  constexpr bool EMIT_STARTUP_JSON = true;
  constexpr bool EMIT_RAW_NMEA_JSON = true;
  constexpr bool EMIT_PARSED_STATE_JSON = true;
}

HardwareSerial gpsSerial(2);

Adafruit_SSD1306 display(
  DisplayConfig::WIDTH,
  DisplayConfig::HEIGHT,
  &Wire,
  DisplayConfig::RESET_PIN
);

enum class DiagnosticState : uint8_t {
  NoData,
  NoFix,
  Degraded2D,
  DegradedLowSat,
  Ok
};

enum class NmeaSentenceType : uint8_t {
  Unknown,
  Gga,
  Gsa,
  Rmc
};

struct GpsData {
  bool nmeaValid = false;

  bool hasFix = false;
  bool locationValid = false;
  bool altitudeValid = false;
  bool geoidValid = false;

  bool fixTypeValid = false;
  bool hdopValid = false;
  bool pdopValid = false;
  bool vdopValid = false;

  bool speedValid = false;
  bool courseValid = false;
  bool timeValid = false;
  bool dateValid = false;

  uint8_t fixType = 0;
  uint8_t fixQuality = 0;
  uint8_t satellitesUsed = 0;

  double latitude = 0.0;
  double longitude = 0.0;
  double altitudeM = 0.0;
  double geoidSeparationM = 0.0;

  double hdop = 0.0;
  double pdop = 0.0;
  double vdop = 0.0;

  double speedKnots = 0.0;
  double speedKmh = 0.0;
  double courseDeg = 0.0;

  char utcTime[16] = "";
  char utcDate[8] = "";

  uint32_t lastNmeaMs = 0;
  uint32_t lastGgaMs = 0;
  uint32_t lastGsaMs = 0;
  uint32_t lastRmcMs = 0;

  uint32_t acceptedSentenceCount = 0;
  uint32_t rawSentenceCount = 0;
  uint32_t checksumErrorCount = 0;
  uint32_t bufferOverflowCount = 0;
};

struct GpsValiditySnapshot {
  uint32_t nowMs = 0;

  bool freshNmea = false;
  bool freshGga = false;
  bool freshGsa = false;
  bool freshRmc = false;

  bool freshPositionSource = false;
  bool freshFixSource = false;
  bool currentFix = false;
  bool usablePosition = false;
  bool usableAltitude = false;
  bool usableHdop = false;

  uint32_t nmeaAgeMs = 0;
  uint32_t ggaAgeMs = 0;
  uint32_t gsaAgeMs = 0;
  uint32_t rmcAgeMs = 0;

  DiagnosticState diagnosticState = DiagnosticState::NoData;
};

GpsData gps;

bool displayReady = false;
uint8_t detectedOledAddress = 0;

char nmeaLine[NmeaConfig::BUFFER_SIZE];
size_t nmeaLinePos = 0;

uint32_t lastDisplayUpdateMs = 0;
uint32_t lastParsedJsonReportMs = 0;

static uint32_t elapsedMs(uint32_t nowMs, uint32_t timestampMs) {
  return nowMs - timestampMs;
}

static bool isRecent(uint32_t nowMs, uint32_t timestampMs) {
  if (timestampMs == 0) {
    return false;
  }

  return elapsedMs(nowMs, timestampMs) <= TimingConfig::GPS_DATA_TIMEOUT_MS;
}

static DiagnosticState calculateDiagnosticState(const GpsValiditySnapshot &snapshot) {
  if (!snapshot.freshNmea) {
    return DiagnosticState::NoData;
  }

  if (!snapshot.usablePosition) {
    return DiagnosticState::NoFix;
  }

  if (snapshot.freshGsa && gps.fixTypeValid && gps.fixType == 2) {
    return DiagnosticState::Degraded2D;
  }

  if (!snapshot.freshGga || gps.satellitesUsed < GpsConfig::MIN_OK_SATELLITES) {
    return DiagnosticState::DegradedLowSat;
  }

  return DiagnosticState::Ok;
}

static GpsValiditySnapshot buildGpsSnapshot(uint32_t nowMs) {
  GpsValiditySnapshot snapshot;
  snapshot.nowMs = nowMs;

  snapshot.freshNmea = gps.nmeaValid && isRecent(nowMs, gps.lastNmeaMs);
  snapshot.freshGga = isRecent(nowMs, gps.lastGgaMs);
  snapshot.freshGsa = isRecent(nowMs, gps.lastGsaMs);
  snapshot.freshRmc = isRecent(nowMs, gps.lastRmcMs);

  snapshot.freshPositionSource = snapshot.freshGga || snapshot.freshRmc;
  snapshot.freshFixSource = snapshot.freshGga || snapshot.freshGsa || snapshot.freshRmc;
  snapshot.currentFix = snapshot.freshFixSource && gps.hasFix;
  snapshot.usablePosition = snapshot.freshPositionSource && snapshot.currentFix && gps.locationValid;

  snapshot.usableAltitude = snapshot.freshGga && gps.altitudeValid && snapshot.usablePosition;
  if (snapshot.usableAltitude && snapshot.freshGsa && gps.fixTypeValid && gps.fixType == 2) {
    snapshot.usableAltitude = false;
  }

  snapshot.usableHdop = (snapshot.freshGga || snapshot.freshGsa) && gps.hdopValid;

  snapshot.nmeaAgeMs = snapshot.freshNmea ? elapsedMs(nowMs, gps.lastNmeaMs) : 0;
  snapshot.ggaAgeMs = snapshot.freshGga ? elapsedMs(nowMs, gps.lastGgaMs) : 0;
  snapshot.gsaAgeMs = snapshot.freshGsa ? elapsedMs(nowMs, gps.lastGsaMs) : 0;
  snapshot.rmcAgeMs = snapshot.freshRmc ? elapsedMs(nowMs, gps.lastRmcMs) : 0;

  snapshot.diagnosticState = calculateDiagnosticState(snapshot);

  return snapshot;
}

static bool isHexDigit(char c) {
  return (c >= '0' && c <= '9') ||
         (c >= 'A' && c <= 'F') ||
         (c >= 'a' && c <= 'f');
}

static bool endsWith(const char *text, const char *suffix) {
  if (!text || !suffix) {
    return false;
  }

  const size_t textLen = strlen(text);
  const size_t suffixLen = strlen(suffix);

  if (textLen < suffixLen) {
    return false;
  }

  return strcmp(text + textLen - suffixLen, suffix) == 0;
}

static bool verifyNmeaChecksum(const char *sentence) {
  if (!sentence || sentence[0] != '$') {
    return false;
  }

  const char *star = strchr(sentence, '*');

  if (!star) {
    return !NmeaConfig::REQUIRE_CHECKSUM;
  }

  if (star[1] == '\0' || star[2] == '\0') {
    return false;
  }

  if (!isHexDigit(star[1]) || !isHexDigit(star[2])) {
    return false;
  }

  uint8_t calculated = 0;

  for (const char *p = sentence + 1; p < star; p++) {
    calculated ^= static_cast<uint8_t>(*p);
  }

  const uint8_t expected = static_cast<uint8_t>(strtoul(star + 1, nullptr, 16));

  return calculated == expected;
}

static int splitCsv(char *text, char *fields[], int maxFields) {
  if (!text || !fields || maxFields <= 0) {
    return 0;
  }

  int count = 0;
  fields[count++] = text;

  for (char *p = text; *p && count < maxFields; p++) {
    if (*p == ',') {
      *p = '\0';
      fields[count++] = p + 1;
    }
  }

  return count;
}

static double parseCoordinate(const char *coordinate, const char *hemisphere) {
  if (!coordinate || !coordinate[0] || !hemisphere || !hemisphere[0]) {
    return 0.0;
  }

  const double raw = atof(coordinate);
  const int degrees = static_cast<int>(raw / 100.0);
  const double minutes = raw - static_cast<double>(degrees) * 100.0;

  double decimalDegrees = static_cast<double>(degrees) + minutes / 60.0;

  if (hemisphere[0] == 'S' || hemisphere[0] == 'W') {
    decimalDegrees = -decimalDegrees;
  }

  return decimalDegrees;
}

static void copyTextField(char *destination, size_t destinationSize, const char *source) {
  if (!destination || destinationSize == 0) {
    return;
  }

  if (!source || !source[0]) {
    destination[0] = '\0';
    return;
  }

  strncpy(destination, source, destinationSize - 1);
  destination[destinationSize - 1] = '\0';
}

static void markPositionInvalid() {
  gps.hasFix = false;
  gps.locationValid = false;
}

static NmeaSentenceType getSentenceType(const char *sentenceId) {
  if (endsWith(sentenceId, "GGA")) {
    return NmeaSentenceType::Gga;
  }

  if (endsWith(sentenceId, "GSA")) {
    return NmeaSentenceType::Gsa;
  }

  if (endsWith(sentenceId, "RMC")) {
    return NmeaSentenceType::Rmc;
  }

  return NmeaSentenceType::Unknown;
}

static const char *sentenceTypeToText(NmeaSentenceType type) {
  switch (type) {
    case NmeaSentenceType::Gga:
      return "GGA";
    case NmeaSentenceType::Gsa:
      return "GSA";
    case NmeaSentenceType::Rmc:
      return "RMC";
    case NmeaSentenceType::Unknown:
      return "UNKNOWN";
  }

  return "UNKNOWN";
}

static NmeaSentenceType detectSentenceTypeFromRaw(const char *sentence) {
  if (!sentence || sentence[0] != '$') {
    return NmeaSentenceType::Unknown;
  }

  char sentenceId[8];
  size_t index = 0;

  for (const char *p = sentence + 1; *p && *p != ',' && *p != '*' && index < sizeof(sentenceId) - 1; p++) {
    sentenceId[index++] = *p;
  }

  sentenceId[index] = '\0';

  if (index == 0) {
    return NmeaSentenceType::Unknown;
  }

  return getSentenceType(sentenceId);
}

static void parseGga(char *fields[], int count, uint32_t nowMs) {
  if (count < 8) {
    return;
  }

  gps.lastGgaMs = nowMs;

  if (fields[1][0]) {
    copyTextField(gps.utcTime, sizeof(gps.utcTime), fields[1]);
    gps.timeValid = true;
  }

  gps.fixQuality = static_cast<uint8_t>(atoi(fields[6]));
  gps.hasFix = gps.fixQuality > 0;
  gps.satellitesUsed = static_cast<uint8_t>(atoi(fields[7]));

  if (count > 8 && fields[8][0]) {
    gps.hdop = atof(fields[8]);
    gps.hdopValid = true;
  } else {
    gps.hdopValid = false;
  }

  if (count > 9 && fields[9][0]) {
    gps.altitudeM = atof(fields[9]);
    gps.altitudeValid = true;
  } else {
    gps.altitudeValid = false;
  }

  if (count > 11 && fields[11][0]) {
    gps.geoidSeparationM = atof(fields[11]);
    gps.geoidValid = true;
  } else {
    gps.geoidSeparationM = 0.0;
    gps.geoidValid = false;
  }

  if (
    gps.hasFix &&
    count >= 6 &&
    fields[2][0] &&
    fields[3][0] &&
    fields[4][0] &&
    fields[5][0]
  ) {
    gps.latitude = parseCoordinate(fields[2], fields[3]);
    gps.longitude = parseCoordinate(fields[4], fields[5]);
    gps.locationValid = true;
  } else {
    gps.locationValid = false;
  }
}

static void parseGsa(char *fields[], int count, uint32_t nowMs) {
  if (count < 3) {
    return;
  }

  gps.lastGsaMs = nowMs;

  const uint8_t nmeaFixType = static_cast<uint8_t>(atoi(fields[2]));
  gps.fixTypeValid = true;

  if (nmeaFixType == 2 || nmeaFixType == 3) {
    gps.fixType = nmeaFixType;
    gps.hasFix = true;
  } else {
    gps.fixType = 0;
    markPositionInvalid();
  }

  if (count > 15 && fields[15][0]) {
    gps.pdop = atof(fields[15]);
    gps.pdopValid = true;
  } else {
    gps.pdopValid = false;
  }

  if (count > 16 && fields[16][0]) {
    gps.hdop = atof(fields[16]);
    gps.hdopValid = true;
  }

  if (count > 17 && fields[17][0]) {
    gps.vdop = atof(fields[17]);
    gps.vdopValid = true;
  } else {
    gps.vdopValid = false;
  }
}

static void parseRmc(char *fields[], int count, uint32_t nowMs) {
  if (count < 7) {
    return;
  }

  gps.lastRmcMs = nowMs;

  if (fields[1][0]) {
    copyTextField(gps.utcTime, sizeof(gps.utcTime), fields[1]);
    gps.timeValid = true;
  }

  const bool active = fields[2][0] == 'A';

  if (!active) {
    markPositionInvalid();
  } else {
    gps.hasFix = true;

    if (fields[3][0] && fields[4][0] && fields[5][0] && fields[6][0]) {
      gps.latitude = parseCoordinate(fields[3], fields[4]);
      gps.longitude = parseCoordinate(fields[5], fields[6]);
      gps.locationValid = true;
    } else {
      gps.locationValid = false;
    }
  }

  if (count > 7 && fields[7][0]) {
    gps.speedKnots = atof(fields[7]);
    gps.speedKmh = gps.speedKnots * 1.852;
    gps.speedValid = true;
  } else {
    gps.speedValid = false;
  }

  if (count > 8 && fields[8][0]) {
    gps.courseDeg = atof(fields[8]);
    gps.courseValid = true;
  } else {
    gps.courseValid = false;
  }

  if (count > 9 && fields[9][0]) {
    copyTextField(gps.utcDate, sizeof(gps.utcDate), fields[9]);
    gps.dateValid = true;
  } else {
    gps.dateValid = false;
  }
}

static void printJsonEscapedString(const char *value) {
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

static void printJsonStringOrNull(bool valid, const char *value) {
  if (valid && value && value[0]) {
    printJsonEscapedString(value);
  } else {
    Serial.print(F("null"));
  }
}

static void printJsonNumberOrNull(bool valid, double value, uint8_t decimals) {
  if (valid) {
    Serial.print(value, decimals);
  } else {
    Serial.print(F("null"));
  }
}

static void printJsonAgeOrNull(bool valid, uint32_t ageMs) {
  if (valid) {
    Serial.print(static_cast<unsigned long>(ageMs));
  } else {
    Serial.print(F("null"));
  }
}

static void reportRawNmeaJson(const char *sentence, bool checksumOk) {
  if (!OutputConfig::EMIT_RAW_NMEA_JSON) {
    return;
  }

  Serial.print(F("{\"type\":\"raw_nmea\",\"millis\":"));
  Serial.print(static_cast<unsigned long>(millis()));

  Serial.print(F(",\"checksumOk\":"));
  Serial.print(checksumOk ? F("true") : F("false"));

  Serial.print(F(",\"sentenceType\":"));
  printJsonEscapedString(sentenceTypeToText(detectSentenceTypeFromRaw(sentence)));

  Serial.print(F(",\"sentence\":"));
  printJsonEscapedString(sentence);

  Serial.println(F("}"));
}

static void processNmeaSentence(const char *sentence) {
  gps.rawSentenceCount++;

  const bool checksumOk = verifyNmeaChecksum(sentence);
  reportRawNmeaJson(sentence, checksumOk);

  if (!checksumOk) {
    gps.checksumErrorCount++;
    return;
  }

  char work[NmeaConfig::BUFFER_SIZE];
  strncpy(work, sentence, sizeof(work) - 1);
  work[sizeof(work) - 1] = '\0';

  char *payload = work;

  if (payload[0] == '$') {
    payload++;
  }

  char *star = strchr(payload, '*');

  if (star) {
    *star = '\0';
  }

  char *fields[NmeaConfig::MAX_FIELDS];
  const int fieldCount = splitCsv(payload, fields, static_cast<int>(NmeaConfig::MAX_FIELDS));

  if (fieldCount == 0 || !fields[0] || !fields[0][0]) {
    return;
  }

  const uint32_t nowMs = millis();
  const NmeaSentenceType sentenceType = getSentenceType(fields[0]);

  gps.nmeaValid = true;
  gps.lastNmeaMs = nowMs;
  gps.acceptedSentenceCount++;

  switch (sentenceType) {
    case NmeaSentenceType::Gga:
      parseGga(fields, fieldCount, nowMs);
      break;

    case NmeaSentenceType::Gsa:
      parseGsa(fields, fieldCount, nowMs);
      break;

    case NmeaSentenceType::Rmc:
      parseRmc(fields, fieldCount, nowMs);
      break;

    case NmeaSentenceType::Unknown:
      break;
  }
}

static void resetNmeaLine() {
  nmeaLinePos = 0;
  nmeaLine[0] = '\0';
}

static void readGpsSerial() {
  while (gpsSerial.available()) {
    const char c = static_cast<char>(gpsSerial.read());

    if (c == '\r') {
      continue;
    }

    if (c == '$') {
      resetNmeaLine();
      nmeaLine[nmeaLinePos++] = c;
      continue;
    }

    if (c == '\n') {
      if (nmeaLinePos > 0 && nmeaLine[0] == '$') {
        nmeaLine[nmeaLinePos] = '\0';
        processNmeaSentence(nmeaLine);
      }

      resetNmeaLine();
      continue;
    }

    if (nmeaLinePos == 0) {
      continue;
    }

    if (nmeaLinePos < sizeof(nmeaLine) - 1) {
      nmeaLine[nmeaLinePos++] = c;
    } else {
      gps.bufferOverflowCount++;
      resetNmeaLine();
    }
  }
}

static const char *diagnosticStateToJson(DiagnosticState state) {
  switch (state) {
    case DiagnosticState::NoData: return "NO_GPS_DATA";
    case DiagnosticState::NoFix: return "NO_FIX";
    case DiagnosticState::Degraded2D: return "DEGRADED_2D";
    case DiagnosticState::DegradedLowSat: return "DEGRADED_LOW_SAT";
    case DiagnosticState::Ok: return "REFERENCE_OK";
  }

  return "UNKNOWN";
}

static const char *diagnosticStateToDisplay(DiagnosticState state) {
  switch (state) {
    case DiagnosticState::NoData: return "NO DATA";
    case DiagnosticState::NoFix: return "NO FIX";
    case DiagnosticState::Degraded2D: return "WARN 2D";
    case DiagnosticState::DegradedLowSat: return "LOW SAT";
    case DiagnosticState::Ok: return "OK";
  }

  return "---";
}

static const char *fixTypeToText(const GpsValiditySnapshot &snapshot) {
  if (!snapshot.freshFixSource || !gps.hasFix) {
    return "NONE";
  }

  if (snapshot.freshGsa && gps.fixTypeValid) {
    if (gps.fixType == 2) return "2D";
    if (gps.fixType == 3) return "3D";
  }

  return "UNKNOWN";
}

static const char *fixTypeToDisplay(const GpsValiditySnapshot &snapshot) {
  if (!snapshot.freshFixSource) {
    return "---";
  }

  if (!gps.hasFix) {
    return "NO";
  }

  if (snapshot.freshGsa && gps.fixTypeValid) {
    if (gps.fixType == 2) return "2D";
    if (gps.fixType == 3) return "3D";
  }

  return "YES";
}

// LED_DATA (GPIO21) and LED_ERROR (GPIO23) are adjacent to I2C SDA and SCL
// respectively.  OUTPUT LOW on these pins would pull the bus low and corrupt
// display communication, so they use INPUT_PULLDOWN when off.
//
// LED_WARNING (GPIO5) and LED_OK (GPIO25) are not adjacent to any I2C pin.
// They use OUTPUT LOW when off: the 47 kΩ INPUT_PULLDOWN is too weak to
// overcome leakage from the neighbouring GPS_TX (GPIO17) line, which idles
// HIGH and causes dim illumination.
static void setLedState(uint8_t pin, bool on) {
  if (on) {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, HIGH);
  } else if (pin == PinConfig::LED_DATA || pin == PinConfig::LED_ERROR) {
    pinMode(pin, INPUT_PULLDOWN);   // I2C-adjacent – cannot drive LOW
  } else {
    pinMode(pin, OUTPUT);
    digitalWrite(pin, LOW);         // not I2C-adjacent – OUTPUT LOW is safe
  }
}

static void setLedPattern(bool errorLed, bool dataLed, bool warningLed, bool okLed) {
  setLedState(PinConfig::LED_ERROR, errorLed);
  setLedState(PinConfig::LED_DATA, dataLed);
  setLedState(PinConfig::LED_WARNING, warningLed);
  setLedState(PinConfig::LED_OK, okLed);
}

static void setAllLedsLow() {
  setLedPattern(false, false, false, false);
}

static void updateLeds(const GpsValiditySnapshot &snapshot) {
  const bool blue   = snapshot.freshNmea;
  const bool red    = snapshot.diagnosticState == DiagnosticState::NoData ||
                      snapshot.diagnosticState == DiagnosticState::NoFix;
  const bool yellow = snapshot.diagnosticState == DiagnosticState::Degraded2D ||
                      snapshot.diagnosticState == DiagnosticState::DegradedLowSat;
  const bool green  = snapshot.diagnosticState == DiagnosticState::Ok;

  setLedPattern(red, blue, yellow, green);
}

static void drawDisplayRow(uint8_t row, const char *label, const char *value) {
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

static void formatDisplayAge(const GpsValiditySnapshot &snapshot, char *buffer, size_t bufferSize) {
  if (!buffer || bufferSize == 0) {
    return;
  }

  if (!gps.nmeaValid) {
    snprintf(buffer, bufferSize, "---");
    return;
  }

  if (!snapshot.freshNmea) {
    snprintf(buffer, bufferSize, "STALE");
    return;
  }

  if (snapshot.nmeaAgeMs < 1000) {
    snprintf(buffer, bufferSize, "<1s");
    return;
  }

  const uint32_t ageSeconds = snapshot.nmeaAgeMs / 1000;
  snprintf(buffer, bufferSize, "%lus", static_cast<unsigned long>(ageSeconds));
}

static void updateDisplay(const GpsValiditySnapshot &snapshot) {
  if (!displayReady) {
    return;
  }

  char value[24];

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  snprintf(value, sizeof(value), "%s", diagnosticStateToDisplay(snapshot.diagnosticState));
  drawDisplayRow(0, "STATE", value);

  snprintf(value, sizeof(value), "%s", fixTypeToDisplay(snapshot));
  drawDisplayRow(1, "FIX", value);

  if (gps.lastGgaMs != 0) {
    snprintf(value, sizeof(value), "%02u", static_cast<unsigned int>(gps.satellitesUsed));
  } else {
    snprintf(value, sizeof(value), "---");
  }
  drawDisplayRow(2, "SATS", value);

  if (snapshot.usablePosition) {
    snprintf(value, sizeof(value), "%.6f", gps.latitude);
  } else {
    snprintf(value, sizeof(value), "---");
  }
  drawDisplayRow(3, "LAT", value);

  if (snapshot.usablePosition) {
    snprintf(value, sizeof(value), "%.6f", gps.longitude);
  } else {
    snprintf(value, sizeof(value), "---");
  }
  drawDisplayRow(4, "LON", value);

  if (snapshot.usableAltitude) {
    snprintf(value, sizeof(value), "%.1fm", gps.altitudeM);
  } else {
    snprintf(value, sizeof(value), "---");
  }
  drawDisplayRow(5, "ALT", value);

  if (snapshot.usableHdop) {
    snprintf(value, sizeof(value), "%.1f", gps.hdop);
  } else {
    snprintf(value, sizeof(value), "---");
  }
  drawDisplayRow(6, "HDOP", value);

  formatDisplayAge(snapshot, value, sizeof(value));
  drawDisplayRow(7, "AGE", value);

  display.display();
}

static void reportStartupJson() {
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
  if (detectedOledAddress != 0) {
    Serial.print(detectedOledAddress, HEX);
  } else {
    Serial.print(F("null"));
  }

  Serial.print(F(",\"displayReady\":"));
  Serial.print(displayReady ? F("true") : F("false"));

  Serial.println(F("}"));
}

static void reportParsedStateJson(const GpsValiditySnapshot &snapshot) {
  if (!OutputConfig::EMIT_PARSED_STATE_JSON) {
    return;
  }

  Serial.print(F("{\"type\":\"parsed_state\""));

  Serial.print(F(",\"millis\":"));
  Serial.print(static_cast<unsigned long>(snapshot.nowMs));

  Serial.print(F(",\"state\":"));
  printJsonEscapedString(diagnosticStateToJson(snapshot.diagnosticState));

  Serial.print(F(",\"valid\":"));
  Serial.print(snapshot.usablePosition ? F("true") : F("false"));

  Serial.print(F(",\"gpsData\":"));
  Serial.print(snapshot.freshNmea ? F("true") : F("false"));

  Serial.print(F(",\"displayReady\":"));
  Serial.print(displayReady ? F("true") : F("false"));

  Serial.print(F(",\"fix\":"));
  Serial.print(snapshot.currentFix ? F("true") : F("false"));

  Serial.print(F(",\"fixType\":"));
  printJsonEscapedString(fixTypeToText(snapshot));

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

static void initializeLeds() {
  setAllLedsLow();
}

// Adafruit_SSD1306::begin() always returns true if malloc succeeds, regardless
// of whether a display is present.  Probe I2C manually before calling begin().
static uint8_t probeOledAddress() {
  Wire.end();
  pinMode(PinConfig::OLED_SDA, INPUT);
  pinMode(PinConfig::OLED_SCL, INPUT);
  delay(5);

  Wire.begin(PinConfig::OLED_SDA, PinConfig::OLED_SCL);
  Wire.setClock(100000);  // 100 kHz – more reliable than 400 kHz on cheap modules
  delay(10);

  const uint8_t candidates[] = {
    DisplayConfig::I2C_ADDRESS,
    DisplayConfig::I2C_ADDRESS_ALT
  };

  for (uint8_t i = 0; i < sizeof(candidates); i++) {
    Wire.beginTransmission(candidates[i]);
    if (Wire.endTransmission() == 0) {
      return candidates[i];
    }
  }

  return 0;
}

static void initializeDisplay() {
  detectedOledAddress = probeOledAddress();

  if (detectedOledAddress == 0) {
    Serial.println(F("{\"type\":\"error\",\"msg\":\"OLED not found on I2C – check SDA/SCL/VCC wiring\"}"));
    return;
  }

  // periphBegin=false: Wire is already initialised by probeOledAddress().
  displayReady = display.begin(SSD1306_SWITCHCAPVCC, detectedOledAddress, false, false);

  if (!displayReady) {
    Serial.println(F("{\"type\":\"error\",\"msg\":\"OLED begin() failed (out of memory)\"}"));
    return;
  }

  display.clearDisplay();
  display.setTextSize(1);
  display.setTextColor(SSD1306_WHITE);

  drawDisplayRow(0, "STATE", "START");
  drawDisplayRow(1, "FIX",   "---");
  drawDisplayRow(2, "SATS",  "---");
  drawDisplayRow(3, "LAT",   "---");
  drawDisplayRow(4, "LON",   "---");
  drawDisplayRow(5, "ALT",   "---");
  drawDisplayRow(6, "HDOP",  "---");
  drawDisplayRow(7, "AGE",   "---");

  display.display();
}

void setup() {
  Serial.begin(UsbConfig::BAUD_RATE);
  delay(500);  // allow power rails and OLED to stabilise

  // Display must be initialised before LEDs: adjacent LED pins (e.g. GPIO5
  // next to GPIO17/GPS_TX) can disturb SDA if driven during I2C probing.
  initializeDisplay();
  initializeLeds();

  gpsSerial.begin(GpsConfig::BAUD_RATE, SERIAL_8N1, PinConfig::GPS_RX, PinConfig::GPS_TX);
  reportStartupJson();
}

void loop() {
  readGpsSerial();

  const uint32_t nowMs = millis();
  const GpsValiditySnapshot snapshot = buildGpsSnapshot(nowMs);

  updateLeds(snapshot);

  if (nowMs - lastDisplayUpdateMs >= TimingConfig::DISPLAY_REFRESH_MS) {
    lastDisplayUpdateMs = nowMs;
    updateDisplay(snapshot);
  }

  if (nowMs - lastParsedJsonReportMs >= TimingConfig::PARSED_REPORT_INTERVAL_MS) {
    lastParsedJsonReportMs = nowMs;
    reportParsedStateJson(snapshot);
  }

  delay(TimingConfig::LOOP_DELAY_MS);
}

