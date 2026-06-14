#include <cmath>
#include <cstdint>
#include <cstdlib>
#include <iostream>
#include <string>

#include "../../firmware/gps_reference_module/src/gps_processing.h"
#include "../../firmware/gps_reference_module/src/status_presentation.h"
#include "../../firmware/gps_reference_module/src/nmea_stream_framer.h"

using GpsProcessing::DiagnosticState;
using GpsProcessing::GpsData;
using GpsProcessing::GpsValiditySnapshot;

namespace {

void runTest(const char *name, const char *description, void (*test)()) {
  test();
  std::cout << "PASS\t" << name << '\t' << description << '\n';
}

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

bool nearlyEqual(double lhs, double rhs, double epsilon = 1e-6) {
  return std::fabs(lhs - rhs) <= epsilon;
}

/**
 * Purpose: Validate the NMEA checksum acceptance boundary.
 * Setup: Exercise null, malformed, valid, invalid, required, and optional checksums.
 * Verifies: Only structurally valid sentences with an acceptable checksum policy pass.
 */
void testChecksumValidation() {
  require(!GpsProcessing::verifyNmeaChecksum(nullptr, false), "null sentence should fail");
  require(!GpsProcessing::verifyNmeaChecksum("GPGGA,123519", false),
          "sentence without $ should fail");
  require(GpsProcessing::verifyNmeaChecksum(
              "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47", true),
          "valid checksum should pass");
  require(!GpsProcessing::verifyNmeaChecksum(
              "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00", true),
          "invalid checksum should fail");
  require(GpsProcessing::verifyNmeaChecksum(
              "$GPRMC,120000.00,A,5001.000,N,01957.000,E,0.10,90.0,250526,,,A", false),
          "checksum-less sentence should pass when checksum is optional");
  require(!GpsProcessing::verifyNmeaChecksum("$GPRMC,120000.00,A", true),
          "checksum-less sentence should fail when checksum is required");
  require(!GpsProcessing::verifyNmeaChecksum("$GPGGA,123519*4", true),
          "one-digit checksum should fail");
  require(!GpsProcessing::verifyNmeaChecksum("$GPGGA,123519*ZZ", true),
          "non-hex checksum should fail");
  require(!GpsProcessing::verifyNmeaChecksum(
              "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*4700", true),
          "checksum with trailing data should fail");
}

/**
 * Purpose: Protect sentence routing and externally visible sentence labels.
 * Setup: Provide supported GP/GN/GL talkers, an unsupported sentence, and null input.
 * Verifies: GGA/GSA/RMC detection is talker-independent and unknown input is stable.
 */
void testSentenceTypeDetectionAndLabels() {
  using GpsProcessing::NmeaSentenceType;

  require(GpsProcessing::detectSentenceTypeFromRaw("$GNGGA,120000.00") == NmeaSentenceType::Gga,
          "GN talker GGA should be detected");
  require(GpsProcessing::detectSentenceTypeFromRaw("$GLGSA,A,3") == NmeaSentenceType::Gsa,
          "GL talker GSA should be detected");
  require(GpsProcessing::detectSentenceTypeFromRaw("$GPRMC,120000.00") == NmeaSentenceType::Rmc,
          "GP talker RMC should be detected");
  require(GpsProcessing::detectSentenceTypeFromRaw("$GPTXT,01,01,02") == NmeaSentenceType::Unknown,
          "unsupported sentence should be unknown");
  require(GpsProcessing::detectSentenceTypeFromRaw(nullptr) == NmeaSentenceType::Unknown,
          "null sentence should be unknown");

  require(std::string(GpsProcessing::sentenceTypeToText(NmeaSentenceType::Gga)) == "GGA",
          "GGA label mismatch");
  require(std::string(GpsProcessing::sentenceTypeToText(NmeaSentenceType::Gsa)) == "GSA",
          "GSA label mismatch");
  require(std::string(GpsProcessing::sentenceTypeToText(NmeaSentenceType::Rmc)) == "RMC",
          "RMC label mismatch");
  require(std::string(GpsProcessing::sentenceTypeToText(NmeaSentenceType::Unknown)) == "UNKNOWN",
          "unknown label mismatch");
}

/**
 * Purpose: Validate extraction of the position fields carried by a GGA sentence.
 * Setup: Parse a known northern/eastern fix with altitude, geoid, satellites, and HDOP.
 * Verifies: Values, validity flags, fix state, and the source timestamp are populated.
 */
void testGgaParsing() {
  GpsData gps;
  GpsProcessing::processNmeaSentence(
      gps, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,", 1000, false);

  require(gps.nmeaValid, "GGA should mark NMEA valid");
  require(gps.hasFix, "GGA with fixQuality=1 should mark fix");
  require(gps.locationValid, "GGA with coordinates should mark location valid");
  require(gps.altitudeValid, "GGA altitude should be valid");
  require(gps.geoidValid, "GGA geoid should be valid");
  require(gps.hdopValid, "GGA HDOP should be valid");
  require(gps.satellitesUsed == 8, "GGA should parse satellites");
  require(nearlyEqual(gps.latitude, 48.1173), "GGA latitude parse mismatch");
  require(nearlyEqual(gps.longitude, 11.5166667), "GGA longitude parse mismatch");
  require(nearlyEqual(gps.altitudeM, 545.4), "GGA altitude parse mismatch");
  require(gps.lastGgaMs == 1000, "GGA timestamp mismatch");

  GpsProcessing::processNmeaSentence(
      gps, "$GPGGA,123520,4807.038,N,01131.000,E,bad,-1,nan,oops,M,inf,M,,", 1100, false);
  require(!gps.hasFix, "malformed fix quality should clear fix");
  require(gps.fixQuality == 0, "malformed fix quality should report zero");
  require(gps.satellitesUsed == 0, "malformed satellite count should report zero");
  require(!gps.hdopValid, "non-finite GGA HDOP should be invalid");
  require(!gps.altitudeValid, "malformed altitude should be invalid");
  require(!gps.geoidValid, "non-finite geoid separation should be invalid");
}

/**
 * Purpose: Cover signed coordinates and transition from a valid fix to invalid data.
 * Setup: Parse a southern/western fix, then no-fix GGA and void RMC sentences.
 * Verifies: Hemisphere signs are correct and stale location/fix flags are cleared.
 */
void testCoordinatesAndInvalidFixes() {
  GpsData gps;
  GpsProcessing::processNmeaSentence(
      gps, "$GNGGA,123519,3351.1200,S,15112.8400,W,1,09,0.7,25.5,M,30.0,M,,", 100, false);

  require(nearlyEqual(gps.latitude, -33.852, 1e-6), "southern latitude parse mismatch");
  require(nearlyEqual(gps.longitude, -151.214, 1e-6), "western longitude parse mismatch");

  GpsProcessing::processNmeaSentence(gps, "$GNGGA,123520,,,,,0,03,99.9,0.0,M,34.0,M,,", 200, false);
  require(!gps.hasFix, "GGA fix quality zero should clear fix");
  require(!gps.locationValid, "GGA without coordinates should clear location");
  require(gps.satellitesUsed == 3, "no-fix GGA should still report satellites");

  GpsProcessing::processNmeaSentence(gps, "$GNRMC,123521.00,V,,,,,0.00,0.0,260426,,,N", 300, false);
  require(!gps.hasFix, "void RMC should clear fix");
  require(!gps.locationValid, "void RMC should clear location");

  GpsProcessing::processNmeaSentence(
      gps, "$GNGGA,123522,9160.000,N,18100.000,E,1,09,0.7,25.5,M,30.0,M,,", 400, false);
  require(!gps.locationValid, "out-of-range coordinates should be rejected");

  GpsProcessing::processNmeaSentence(
      gps, "$GNGGA,123523,3351.1200,E,15112.8400,N,1,09,0.7,25.5,M,30.0,M,,", 500, false);
  require(!gps.locationValid, "latitude and longitude hemispheres should not be interchangeable");
}

/**
 * Purpose: Validate complementary motion and fix-dimension sentence parsing.
 * Setup: Parse an active RMC sentence followed by a 2D GSA sentence.
 * Verifies: Speed, course, date, position, fix type, and DOP values become valid.
 */
void testRmcAndGsaParsing() {
  GpsData gps;

  GpsProcessing::processNmeaSentence(
      gps, "$GPRMC,120001.00,A,5001.000,N,01957.000,E,1.50,90.0,250526,,,A", 2000, false);
  require(gps.hasFix, "RMC active sentence should set fix");
  require(gps.locationValid, "RMC should set location valid");
  require(gps.speedValid, "RMC should set speed valid");
  require(gps.courseValid, "RMC should set course valid");
  require(gps.dateValid, "RMC should set date valid");
  require(nearlyEqual(gps.speedKmh, 1.50 * 1.852, 1e-3), "RMC speed km/h mismatch");

  GpsProcessing::processNmeaSentence(
      gps, "$GPGSA,A,2,04,05,09,12,24,25,29,31,02,14,18,22,1.8,1.0,1.2", 2100, false);
  require(gps.fixTypeValid, "GSA should set fix type valid");
  require(gps.fixType == 2, "GSA should parse 2D fix");
  require(gps.pdopValid && gps.hdopValid && gps.vdopValid, "GSA should set DOP validity");

  GpsProcessing::processNmeaSentence(gps, "$GPGSA,A,2,04,05,09,12,24,25,29,31,02,14,18,22,1.8,,1.2",
                                     2200, false);
  require(!gps.hdopValid, "GSA without HDOP should clear stale HDOP validity");

  GpsProcessing::processNmeaSentence(
      gps, "$GPGSA,A,bad,04,05,09,12,24,25,29,31,02,14,18,22,nan,-1,inf", 2300, false);
  require(!gps.fixTypeValid, "malformed GSA fix type should be invalid");
  require(!gps.pdopValid && !gps.hdopValid && !gps.vdopValid,
          "malformed or non-finite GSA DOP values should be invalid");

  GpsProcessing::processNmeaSentence(
      gps, "$GPRMC,120002.00,A,5001.000,N,01957.000,E,bad,361,250526,,,A", 2400, false);
  require(!gps.speedValid, "malformed RMC speed should be invalid");
  require(!gps.courseValid, "out-of-range RMC course should be invalid");
}

/**
 * Purpose: Protect observability counters and rejection semantics.
 * Setup: Process one invalid-checksum sentence and one supported-format unknown sentence.
 * Verifies: Raw, accepted, checksum-error, validity, and last-seen fields stay consistent.
 */
void testSentenceCountersAndRejection() {
  GpsData gps;

  GpsProcessing::processNmeaSentence(
      gps, "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00", 100, true);
  require(gps.rawSentenceCount == 1, "rejected sentence should increment raw count");
  require(gps.checksumErrorCount == 1, "rejected sentence should increment checksum errors");
  require(gps.acceptedSentenceCount == 0, "rejected sentence should not increment accepted count");
  require(!gps.nmeaValid, "rejected sentence should not mark NMEA valid");

  GpsProcessing::processNmeaSentence(gps, "$GPTXT,01,01,02,hello", 200, false);
  require(gps.rawSentenceCount == 2, "unknown sentence should increment raw count");
  require(gps.acceptedSentenceCount == 1, "valid unknown sentence should count as accepted NMEA");
  require(gps.nmeaValid, "valid unknown sentence should mark NMEA valid");
  require(gps.lastNmeaMs == 200, "valid unknown sentence should refresh NMEA timestamp");
}

/**
 * Purpose: Validate the diagnostic state machine used by serial, OLED, and LEDs.
 * Setup: Progress one GPS record through 2D, low-satellite 3D, healthy 3D, and stale data.
 * Verifies: Each condition maps to the intended state and 2D altitude is suppressed.
 */
void testSnapshotStates() {
  GpsData gps;

  GpsProcessing::processNmeaSentence(
      gps, "$GPGGA,123519,4807.038,N,01131.000,E,1,05,0.9,545.4,M,46.9,M,,", 1000, false);
  GpsProcessing::processNmeaSentence(
      gps, "$GPGSA,A,2,04,05,09,12,24,25,29,31,02,14,18,22,1.8,1.0,1.2", 1100, false);

  GpsValiditySnapshot degraded2d = GpsProcessing::buildGpsSnapshot(gps, 1200, 1800, 6);
  require(degraded2d.diagnosticState == DiagnosticState::Degraded2D,
          "2D fix should degrade to DEGRADDED_2D");
  require(!degraded2d.usableAltitude, "2D fix should suppress altitude");

  gps.fixType = 3;
  gps.satellitesUsed = 5;
  GpsValiditySnapshot lowSat = GpsProcessing::buildGpsSnapshot(gps, 1300, 1800, 6);
  require(lowSat.diagnosticState == DiagnosticState::DegradedLowSat,
          "3D fix with low satellites should be degraded");

  gps.satellitesUsed = 7;
  GpsValiditySnapshot ok = GpsProcessing::buildGpsSnapshot(gps, 1400, 1800, 6);
  require(ok.diagnosticState == DiagnosticState::Ok, "3D fix with enough satellites should be OK");

  GpsValiditySnapshot stale = GpsProcessing::buildGpsSnapshot(gps, 4000, 1800, 6);
  require(stale.diagnosticState == DiagnosticState::NoData, "stale data should become NO_DATA");
}

/**
 * Purpose: Validate freshness calculations at difficult unsigned-time boundaries.
 * Setup: Evaluate data exactly at timeout, one millisecond late, and across millis rollover.
 * Verifies: Boundary freshness, expiration, age calculation, and diagnostic state are correct.
 */
void testSnapshotBoundariesAndRollover() {
  GpsData gps;
  gps.nmeaValid = true;
  gps.hasFix = true;
  gps.locationValid = true;
  gps.altitudeValid = true;
  gps.hdopValid = true;
  gps.fixTypeValid = true;
  gps.fixType = 3;
  gps.satellitesUsed = 6;
  gps.lastNmeaMs = 100;
  gps.lastGgaMs = 100;
  gps.lastGsaMs = 100;
  gps.lastRmcMs = 100;

  GpsValiditySnapshot boundary = GpsProcessing::buildGpsSnapshot(gps, 1900, 1800, 6);
  require(boundary.freshNmea, "timestamp exactly at timeout should be fresh");
  require(boundary.diagnosticState == DiagnosticState::Ok, "timeout boundary should remain OK");
  require(boundary.nmeaAgeMs == 1800, "timeout boundary age mismatch");

  GpsValiditySnapshot expired = GpsProcessing::buildGpsSnapshot(gps, 1901, 1800, 6);
  require(!expired.freshNmea, "timestamp beyond timeout should be stale");
  require(expired.diagnosticState == DiagnosticState::NoData, "expired data should be NO_DATA");

  gps.lastNmeaMs = UINT32_MAX - 99;
  gps.lastGgaMs = UINT32_MAX - 99;
  gps.lastGsaMs = UINT32_MAX - 99;
  gps.lastRmcMs = UINT32_MAX - 99;
  GpsValiditySnapshot rollover = GpsProcessing::buildGpsSnapshot(gps, 50, 1800, 6);
  require(rollover.freshNmea, "millis rollover should preserve freshness");
  require(rollover.nmeaAgeMs == 150, "millis rollover age mismatch");
  require(rollover.diagnosticState == DiagnosticState::Ok, "millis rollover should preserve state");
}

/**
 * Purpose: Keep protocol and display labels backward-compatible.
 * Setup: Evaluate every diagnostic state plus stale, absent, unknown, 2D, and 3D fixes.
 * Verifies: JSON and OLED text remain stable for downstream consumers and operators.
 */
void testStateAndFixLabels() {
  require(std::string(GpsProcessing::diagnosticStateToJson(DiagnosticState::NoData)) ==
              "NO_GPS_DATA",
          "NoData JSON label mismatch");
  require(std::string(GpsProcessing::diagnosticStateToJson(DiagnosticState::NoFix)) == "NO_FIX",
          "NoFix JSON label mismatch");
  require(std::string(GpsProcessing::diagnosticStateToJson(DiagnosticState::Degraded2D)) ==
              "DEGRADED_2D",
          "2D JSON label mismatch");
  require(std::string(GpsProcessing::diagnosticStateToJson(DiagnosticState::DegradedLowSat)) ==
              "DEGRADED_LOW_SAT",
          "low-sat JSON label mismatch");
  require(std::string(GpsProcessing::diagnosticStateToJson(DiagnosticState::Ok)) == "REFERENCE_OK",
          "OK JSON label mismatch");

  GpsData gps;
  GpsValiditySnapshot snapshot;
  require(std::string(GpsProcessing::fixTypeToText(gps, snapshot)) == "NONE",
          "stale fix JSON label mismatch");
  require(std::string(GpsProcessing::fixTypeToDisplay(gps, snapshot)) == "---",
          "stale fix display label mismatch");

  snapshot.freshFixSource = true;
  require(std::string(GpsProcessing::fixTypeToText(gps, snapshot)) == "NONE",
          "no-fix JSON label mismatch");
  require(std::string(GpsProcessing::fixTypeToDisplay(gps, snapshot)) == "NO",
          "no-fix display label mismatch");

  gps.hasFix = true;
  require(std::string(GpsProcessing::fixTypeToText(gps, snapshot)) == "UNKNOWN",
          "unknown fix JSON label mismatch");
  require(std::string(GpsProcessing::fixTypeToDisplay(gps, snapshot)) == "YES",
          "unknown fix display label mismatch");

  snapshot.freshGsa = true;
  gps.fixTypeValid = true;
  gps.fixType = 2;
  require(std::string(GpsProcessing::fixTypeToText(gps, snapshot)) == "2D",
          "2D fix JSON label mismatch");
  require(std::string(GpsProcessing::fixTypeToDisplay(gps, snapshot)) == "2D",
          "2D fix display label mismatch");

  gps.fixType = 3;
  require(std::string(GpsProcessing::fixTypeToText(gps, snapshot)) == "3D",
          "3D fix JSON label mismatch");
  require(std::string(GpsProcessing::fixTypeToDisplay(gps, snapshot)) == "3D",
          "3D fix display label mismatch");
}

/**
 * Purpose: Validate conversion of an arbitrary UART byte stream into safe NMEA lines.
 * Setup: Feed noise, CRLF data, overflow, recovery, embedded restart, and short output buffers.
 * Verifies: Framing completes correctly, recovers after errors, and never overruns output.
 */
void testNmeaStreamFramer() {
  NmeaStreamFramer::LineAccumulator framer;
  char sentence[144];

  require(framer.feed('x', sentence, sizeof(sentence)) == NmeaStreamFramer::FeedResult::None,
          "noise before sentence should be ignored");

  const char *input = "$GPRMC,120001.00,A,5001.000,N,01957.000,E,1.50,90.0,250526,,,A\r\n";
  NmeaStreamFramer::FeedResult result = NmeaStreamFramer::FeedResult::None;

  for (const char *p = input; *p; ++p) {
    result = framer.feed(*p, sentence, sizeof(sentence));
  }

  require(result == NmeaStreamFramer::FeedResult::Complete, "full sentence should complete");
  require(std::string(sentence) == "$GPRMC,120001.00,A,5001.000,N,01957.000,E,1.50,90.0,250526,,,A",
          "framer output mismatch");

  framer.reset();
  bool sawOverflow = false;
  for (int i = 0; i < 160; ++i) {
    result = framer.feed(i == 0 ? '$' : 'A', sentence, sizeof(sentence));
    if (result == NmeaStreamFramer::FeedResult::Overflow) {
      sawOverflow = true;
    }
  }
  require(sawOverflow, "long sentence should overflow");

  const char *recovery = "$GPGGA,1\n";
  for (const char *p = recovery; *p; ++p) {
    result = framer.feed(*p, sentence, sizeof(sentence));
  }
  require(result == NmeaStreamFramer::FeedResult::Complete, "framer should recover after overflow");
  require(std::string(sentence) == "$GPGGA,1", "framer recovery output mismatch");

  framer.reset();
  const char *restart = "$GPGGA,old$GPRMC,new\n";
  for (const char *p = restart; *p; ++p) {
    result = framer.feed(*p, sentence, sizeof(sentence));
  }
  require(result == NmeaStreamFramer::FeedResult::Complete, "new $ should restart sentence");
  require(std::string(sentence) == "$GPRMC,new", "restart should discard partial sentence");

  framer.reset();
  char shortSentence[6];
  const char *longForOutput = "$GPGGA,123\n";
  for (const char *p = longForOutput; *p; ++p) {
    result = framer.feed(*p, shortSentence, sizeof(shortSentence));
  }
  require(result == NmeaStreamFramer::FeedResult::Complete,
          "truncated output should still complete");
  require(std::string(shortSentence) == "$GPGG", "completed output should be safely truncated");
}

/**
 * Purpose: Validate the final operator-facing presentation derived from GPS state.
 * Setup: Build a healthy display model, enumerate every LED state, then render empty/stale data.
 * Verifies: OLED formatting and error/data/warning/OK LED patterns match diagnostics.
 */
void testPresentationModelsAndLedPatterns() {
  GpsData gps;

  GpsProcessing::processNmeaSentence(
      gps, "$GPGGA,123519,5001.5991,N,01957.2161,E,1,07,0.8,263.1,M,40.0,M,,", 1000, false);
  GpsProcessing::processNmeaSentence(
      gps, "$GPGSA,A,3,04,05,09,12,24,25,29,31,02,14,18,22,1.6,0.8,1.4", 1100, false);

  GpsValiditySnapshot snapshot = GpsProcessing::buildGpsSnapshot(gps, 1200, 1800, 6);
  StatusPresentation::DisplayModel model;
  StatusPresentation::buildDisplayModel(gps, snapshot, model);

  require(std::string(model.state) == "OK", "display state mismatch");
  require(std::string(model.fix) == "3D", "display fix mismatch");
  require(std::string(model.sats) == "07", "display sats mismatch");
  require(std::string(model.altitude) == "263.1m", "display altitude mismatch");
  require(std::string(model.hdop) == "0.8", "display hdop mismatch");
  require(std::string(model.age) == "<1s", "display age mismatch");

  const StatusPresentation::LedPattern pattern = StatusPresentation::buildLedPattern(snapshot);
  require(!pattern.error && pattern.data && !pattern.warning && pattern.ok,
          "OK state LED pattern mismatch");

  struct PatternCase {
    DiagnosticState state;
    bool freshNmea;
    bool error;
    bool data;
    bool warning;
    bool ok;
  };

  const PatternCase cases[] = {
      {DiagnosticState::NoData, false, true, false, false, false},
      {DiagnosticState::NoFix, true, true, true, false, false},
      {DiagnosticState::Degraded2D, true, false, true, true, false},
      {DiagnosticState::DegradedLowSat, true, false, true, true, false},
      {DiagnosticState::Ok, true, false, true, false, true},
  };

  for (const PatternCase &item : cases) {
    GpsValiditySnapshot patternSnapshot;
    patternSnapshot.diagnosticState = item.state;
    patternSnapshot.freshNmea = item.freshNmea;
    const StatusPresentation::LedPattern actual =
        StatusPresentation::buildLedPattern(patternSnapshot);
    require(actual.error == item.error, "LED error output mismatch");
    require(actual.data == item.data, "LED data output mismatch");
    require(actual.warning == item.warning, "LED warning output mismatch");
    require(actual.ok == item.ok, "LED OK output mismatch");
  }

  GpsData emptyGps;
  GpsValiditySnapshot noData;
  noData.diagnosticState = DiagnosticState::NoData;
  StatusPresentation::DisplayModel emptyModel;
  StatusPresentation::buildDisplayModel(emptyGps, noData, emptyModel);
  require(std::string(emptyModel.state) == "NO DATA", "no-data display state mismatch");
  require(std::string(emptyModel.fix) == "---", "no-data display fix mismatch");
  require(std::string(emptyModel.sats) == "---", "no-data display satellites mismatch");
  require(std::string(emptyModel.age) == "---", "never-seen display age mismatch");

  emptyGps.nmeaValid = true;
  emptyGps.lastNmeaMs = 1;
  StatusPresentation::DisplayModel staleModel;
  StatusPresentation::buildDisplayModel(emptyGps, noData, staleModel);
  require(std::string(staleModel.age) == "STALE", "stale display age mismatch");
}

} // namespace

int main() {
  runTest("NMEA checksum validation",
          "Accepts valid or optional checksums and rejects malformed input.",
          testChecksumValidation);
  runTest("Sentence type detection and labels",
          "Recognizes supported talkers and maps sentence types to stable labels.",
          testSentenceTypeDetectionAndLabels);
  runTest("GGA parsing",
          "Extracts fix quality, satellites, coordinates, altitude, geoid, and HDOP.",
          testGgaParsing);
  runTest("Coordinates and invalid fixes",
          "Handles southern/western coordinates and clears stale fix fields.",
          testCoordinatesAndInvalidFixes);
  runTest("RMC and GSA parsing",
          "Extracts motion, date, fix dimension, and dilution-of-precision data.",
          testRmcAndGsaParsing);
  runTest("Sentence counters and rejection",
          "Tracks raw, accepted, unsupported, and checksum-rejected sentences.",
          testSentenceCountersAndRejection);
  runTest("Diagnostic state transitions",
          "Classifies 2D, low-satellite, healthy, and stale GPS states.", testSnapshotStates);
  runTest("Timeout boundaries and millis rollover",
          "Preserves freshness at timeout boundaries and across uint32 rollover.",
          testSnapshotBoundariesAndRollover);
  runTest("State and fix labels",
          "Keeps JSON and OLED labels stable for every diagnostic and fix type.",
          testStateAndFixLabels);
  runTest("NMEA stream framing and recovery",
          "Frames UART lines, handles overflow, restarts, and truncates safely.",
          testNmeaStreamFramer);
  runTest("Display models and LED patterns",
          "Formats OLED fields and maps every diagnostic state to the correct LEDs.",
          testPresentationModelsAndLedPatterns);
  return 0;
}
