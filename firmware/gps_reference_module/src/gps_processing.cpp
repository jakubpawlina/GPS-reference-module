/**
 * @file gps_processing.cpp
 * @brief NMEA 0183 sentence parser and GPS diagnostic-state evaluator.
 *
 * Supported sentence types
 * ------------------------
 * GGA (Global Positioning System Fix Data)
 *   [0] Sentence ID   [1] UTC time   [2] Latitude   [3] N/S
 *   [4] Longitude     [5] E/W        [6] Fix quality [7] Satellites used
 *   [8] HDOP          [9] Altitude   [11] Geoid separation
 *
 * GSA (GNSS DOP and Active Satellites)
 *   [0] Sentence ID   [2] Fix type (1=none, 2=2D, 3=3D)
 *   [15] PDOP         [16] HDOP     [17] VDOP
 *
 * RMC (Recommended Minimum Specific GNSS Data)
 *   [0] Sentence ID   [1] UTC time   [2] Status (A=active, V=void)
 *   [3] Latitude      [4] N/S        [5] Longitude   [6] E/W
 *   [7] Speed (knots) [8] Course     [9] Date (DDMMYY)
 *
 * Talker prefixes GP (GPS), GN (multi-constellation), GL (GLONASS), and
 * GA (Galileo) are all accepted; detection is suffix-based so future talker
 * prefixes are handled automatically.
 *
 * Coordinate format
 * -----------------
 * NMEA encodes coordinates as DDDMM.MMMM (degrees concatenated with decimal
 * minutes).  parseCoordinate() separates the degree part with integer division
 * by 100 and converts to decimal degrees: degrees + minutes / 60.
 * Example: 4807.038 N → 48° + 7.038'/60 = 48.1173° N
 *
 * Freshness and uint32 rollover
 * ------------------------------
 * All timestamps use millis() (uint32_t, wraps every ~49.7 days).  The
 * elapsed time is computed as (nowMs - timestampMs), which is correct even
 * when nowMs has wrapped past zero and timestampMs has not, because unsigned
 * subtraction in C++ wraps modulo 2^32 by definition.
 *
 * @see NMEA 0183 standard, version 4.11
 */

#include "gps_processing.h"

#include <cmath>
#include <cstdlib>
#include <cstring>

#include "firmware_settings.h"

namespace GpsProcessing {

/** @brief Unsigned elapsed time in milliseconds (rollover-safe). */
static uint32_t elapsedMs(uint32_t nowMs, uint32_t timestampMs) {
  return nowMs - timestampMs;
}

/** @brief Return true if @p timestampMs is within the timeout window. */
static bool isRecent(uint32_t nowMs, uint32_t timestampMs, uint32_t gpsDataTimeoutMs) {
  if (timestampMs == 0) {
    return false;
  }

  return elapsedMs(nowMs, timestampMs) <= gpsDataTimeoutMs;
}

/** @brief Check if a character is a hexadecimal digit (0-9, A-F, a-f). */
static bool isHexDigit(char c) {
  return (c >= '0' && c <= '9') || (c >= 'A' && c <= 'F') || (c >= 'a' && c <= 'f');
}

/** @brief Return true if @p text ends with @p suffix. */
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

bool verifyNmeaChecksum(const char *sentence, bool requireChecksum) {
  if (!sentence || sentence[0] != '$') {
    return false;
  }

  const char *star = strchr(sentence, '*');

  if (!star) {
    return !requireChecksum;
  }

  if (star[1] == '\0' || star[2] == '\0' || star[3] != '\0') {
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

/** @brief Split a comma-separated string in place, returning pointers to each field. */
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

/**
 * @brief Parse an NMEA coordinate (DDDMM.MMMM) and hemisphere into decimal degrees.
 * @param[in]  coordinate  Raw NMEA coordinate string.
 * @param[in]  hemisphere  Single-character hemisphere indicator (N/S/E/W).
 * @param[in]  latitude    True for latitude (max 90), false for longitude (max 180).
 * @param[out] result      Decimal degrees on success; negative for S/W.
 * @return True if the coordinate was parsed successfully.
 */
static bool parseCoordinate(const char *coordinate, const char *hemisphere, bool latitude,
                            double &result) {
  if (!coordinate || !coordinate[0] || !hemisphere || !hemisphere[0]) {
    return false;
  }

  if (hemisphere[1] != '\0') {
    return false;
  }

  const bool validHemisphere = latitude ? hemisphere[0] == 'N' || hemisphere[0] == 'S'
                                        : hemisphere[0] == 'E' || hemisphere[0] == 'W';
  if (!validHemisphere) {
    return false;
  }

  char *end = nullptr;
  const double raw = strtod(coordinate, &end);
  if (end == coordinate || *end != '\0' || !std::isfinite(raw) || raw < 0.0) {
    return false;
  }

  const int degrees = static_cast<int>(raw / 100.0);
  const double minutes = raw - static_cast<double>(degrees) * 100.0;
  const int maximumDegrees = latitude ? 90 : 180;
  if (minutes < 0.0 || minutes >= 60.0 || degrees > maximumDegrees) {
    return false;
  }
  if (degrees == maximumDegrees && minutes > 0.0) {
    return false;
  }

  double decimalDegrees = static_cast<double>(degrees) + minutes / 60.0;

  if (hemisphere[0] == 'S' || hemisphere[0] == 'W') {
    decimalDegrees = -decimalDegrees;
  }

  result = decimalDegrees;
  return true;
}

/** @brief Safe string copy with null termination; clears destination if source is empty. */
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

/** @brief Clear both fix and location validity flags. */
static void markPositionInvalid(GpsData &gps) {
  gps.hasFix = false;
  gps.locationValid = false;
}

/** @brief Map a talker+sentence ID suffix to the internal enum. */
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

const char *sentenceTypeToText(NmeaSentenceType type) {
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

NmeaSentenceType detectSentenceTypeFromRaw(const char *sentence) {
  if (!sentence || sentence[0] != '$') {
    return NmeaSentenceType::Unknown;
  }

  char sentenceId[8];
  size_t index = 0;

  for (const char *p = sentence + 1; *p && *p != ',' && *p != '*' && index < sizeof(sentenceId) - 1;
       p++) {
    sentenceId[index++] = *p;
  }

  sentenceId[index] = '\0';

  if (index == 0) {
    return NmeaSentenceType::Unknown;
  }

  return getSentenceType(sentenceId);
}

/** @brief Extract fix, satellites, coordinates, altitude, and HDOP from a GGA sentence. */
static void parseGga(GpsData &gps, char *fields[], int count, uint32_t nowMs) {
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

  if (gps.hasFix && count >= 6 && fields[2][0] && fields[3][0] && fields[4][0] && fields[5][0]) {
    double latitude = 0.0;
    double longitude = 0.0;
    gps.locationValid = parseCoordinate(fields[2], fields[3], true, latitude) &&
                        parseCoordinate(fields[4], fields[5], false, longitude);
    if (gps.locationValid) {
      gps.latitude = latitude;
      gps.longitude = longitude;
    }
  } else {
    gps.locationValid = false;
  }
}

/** @brief Extract fix type (2D/3D) and DOP values from a GSA sentence. */
static void parseGsa(GpsData &gps, char *fields[], int count, uint32_t nowMs) {
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
    markPositionInvalid(gps);
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
  } else {
    gps.hdopValid = false;
  }

  if (count > 17 && fields[17][0]) {
    gps.vdop = atof(fields[17]);
    gps.vdopValid = true;
  } else {
    gps.vdopValid = false;
  }
}

/** @brief Extract position, speed, course, and date from an RMC sentence. */
static void parseRmc(GpsData &gps, char *fields[], int count, uint32_t nowMs) {
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
    markPositionInvalid(gps);
  } else {
    gps.hasFix = true;

    if (fields[3][0] && fields[4][0] && fields[5][0] && fields[6][0]) {
      double latitude = 0.0;
      double longitude = 0.0;
      gps.locationValid = parseCoordinate(fields[3], fields[4], true, latitude) &&
                          parseCoordinate(fields[5], fields[6], false, longitude);
      if (gps.locationValid) {
        gps.latitude = latitude;
        gps.longitude = longitude;
      }
    } else {
      gps.locationValid = false;
    }
  }

  if (count > 7 && fields[7][0]) {
    gps.speedKnots = atof(fields[7]);
    gps.speedKmh = gps.speedKnots * GpsConfig::KNOTS_TO_KMH;
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

/** @brief Increment a counter with saturation at UINT32_MAX. */
static void saturatingIncrement(uint32_t &counter) {
  if (counter < UINT32_MAX) {
    counter++;
  }
}

void processNmeaSentence(GpsData &gps, const char *sentence, uint32_t nowMs, bool requireChecksum,
                         bool checksumAlreadyVerified) {
  saturatingIncrement(gps.rawSentenceCount);

  const bool checksumOk = checksumAlreadyVerified || verifyNmeaChecksum(sentence, requireChecksum);
  if (!checksumOk) {
    saturatingIncrement(gps.checksumErrorCount);
    return;
  }

  char work[NmeaConfig::BUFFER_SIZE];
  if (strlen(sentence) >= sizeof(work)) {
    return;
  }
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

  constexpr int MAX_NMEA_FIELDS = 24;
  char *fields[MAX_NMEA_FIELDS];
  const int fieldCount = splitCsv(payload, fields, MAX_NMEA_FIELDS);
  if (fieldCount == 0 || !fields[0] || !fields[0][0]) {
    return;
  }

  const NmeaSentenceType sentenceType = getSentenceType(fields[0]);

  gps.nmeaValid = true;
  gps.lastNmeaMs = nowMs;
  saturatingIncrement(gps.acceptedSentenceCount);

  switch (sentenceType) {
  case NmeaSentenceType::Gga:
    parseGga(gps, fields, fieldCount, nowMs);
    break;
  case NmeaSentenceType::Gsa:
    parseGsa(gps, fields, fieldCount, nowMs);
    break;
  case NmeaSentenceType::Rmc:
    parseRmc(gps, fields, fieldCount, nowMs);
    break;
  case NmeaSentenceType::Unknown:
    break;
  }
}

/** @brief Classify the current GPS health from snapshot freshness and satellite count. */
static DiagnosticState calculateDiagnosticState(const GpsValiditySnapshot &snapshot,
                                                const GpsData &gps, uint8_t minOkSatellites) {
  if (!snapshot.freshNmea) {
    return DiagnosticState::NoData;
  }

  if (!snapshot.usablePosition) {
    return DiagnosticState::NoFix;
  }

  if (snapshot.freshGsa && gps.fixTypeValid && gps.fixType == 2) {
    return DiagnosticState::Degraded2D;
  }

  if (!snapshot.freshGga || gps.satellitesUsed < minOkSatellites) {
    return DiagnosticState::DegradedLowSat;
  }

  return DiagnosticState::Ok;
}

GpsValiditySnapshot buildGpsSnapshot(const GpsData &gps, uint32_t nowMs, uint32_t gpsDataTimeoutMs,
                                     uint8_t minOkSatellites) {
  GpsValiditySnapshot snapshot;
  snapshot.nowMs = nowMs;

  snapshot.freshNmea = gps.nmeaValid && isRecent(nowMs, gps.lastNmeaMs, gpsDataTimeoutMs);
  snapshot.freshGga = isRecent(nowMs, gps.lastGgaMs, gpsDataTimeoutMs);
  snapshot.freshGsa = isRecent(nowMs, gps.lastGsaMs, gpsDataTimeoutMs);
  snapshot.freshRmc = isRecent(nowMs, gps.lastRmcMs, gpsDataTimeoutMs);

  snapshot.freshPositionSource = snapshot.freshGga || snapshot.freshRmc;
  snapshot.freshFixSource = snapshot.freshGga || snapshot.freshGsa || snapshot.freshRmc;
  snapshot.currentFix = snapshot.freshFixSource && gps.hasFix;
  snapshot.usablePosition =
      snapshot.freshPositionSource && snapshot.currentFix && gps.locationValid;

  snapshot.usableAltitude = snapshot.freshGga && gps.altitudeValid && snapshot.usablePosition;
  if (snapshot.usableAltitude && snapshot.freshGsa && gps.fixTypeValid && gps.fixType == 2) {
    snapshot.usableAltitude = false;
  }

  snapshot.usableHdop = (snapshot.freshGga || snapshot.freshGsa) && gps.hdopValid;

  snapshot.nmeaAgeMs = snapshot.freshNmea ? elapsedMs(nowMs, gps.lastNmeaMs) : 0;
  snapshot.ggaAgeMs = snapshot.freshGga ? elapsedMs(nowMs, gps.lastGgaMs) : 0;
  snapshot.gsaAgeMs = snapshot.freshGsa ? elapsedMs(nowMs, gps.lastGsaMs) : 0;
  snapshot.rmcAgeMs = snapshot.freshRmc ? elapsedMs(nowMs, gps.lastRmcMs) : 0;

  snapshot.diagnosticState = calculateDiagnosticState(snapshot, gps, minOkSatellites);
  return snapshot;
}

const char *diagnosticStateToJson(DiagnosticState state) {
  switch (state) {
  case DiagnosticState::NoData:
    return "NO_GPS_DATA";
  case DiagnosticState::NoFix:
    return "NO_FIX";
  case DiagnosticState::Degraded2D:
    return "DEGRADED_2D";
  case DiagnosticState::DegradedLowSat:
    return "DEGRADED_LOW_SAT";
  case DiagnosticState::Ok:
    return "REFERENCE_OK";
  }

  return "UNKNOWN";
}

const char *diagnosticStateToDisplay(DiagnosticState state) {
  switch (state) {
  case DiagnosticState::NoData:
    return "NO DATA";
  case DiagnosticState::NoFix:
    return "NO FIX";
  case DiagnosticState::Degraded2D:
    return "WARN 2D";
  case DiagnosticState::DegradedLowSat:
    return "LOW SAT";
  case DiagnosticState::Ok:
    return "OK";
  }

  return "---";
}

const char *fixTypeToText(const GpsData &gps, const GpsValiditySnapshot &snapshot) {
  if (!snapshot.freshFixSource || !gps.hasFix) {
    return "NONE";
  }

  if (snapshot.freshGsa && gps.fixTypeValid) {
    if (gps.fixType == 2)
      return "2D";
    if (gps.fixType == 3)
      return "3D";
  }

  return "UNKNOWN";
}

const char *fixTypeToDisplay(const GpsData &gps, const GpsValiditySnapshot &snapshot) {
  if (!snapshot.freshFixSource) {
    return "---";
  }

  if (!gps.hasFix) {
    return "NO";
  }

  if (snapshot.freshGsa && gps.fixTypeValid) {
    if (gps.fixType == 2)
      return "2D";
    if (gps.fixType == 3)
      return "3D";
  }

  return "YES";
}

} // namespace GpsProcessing
