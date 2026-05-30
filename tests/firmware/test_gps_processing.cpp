#include <cmath>
#include <cstdlib>
#include <iostream>

#include "../../firmware/gps_reference_module/src/gps_processing.h"
#include "../../firmware/gps_reference_module/src/status_presentation.h"
#include "../../firmware/gps_reference_module/src/nmea_stream_framer.h"

using GpsProcessing::DiagnosticState;
using GpsProcessing::GpsData;
using GpsProcessing::GpsValiditySnapshot;

namespace {

void require(bool condition, const char *message) {
  if (!condition) {
    std::cerr << "FAILED: " << message << '\n';
    std::exit(1);
  }
}

bool nearlyEqual(double lhs, double rhs, double epsilon = 1e-6) {
  return std::fabs(lhs - rhs) <= epsilon;
}

void testChecksumValidation() {
  require(
    GpsProcessing::verifyNmeaChecksum("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*47", true),
    "valid checksum should pass"
  );
  require(
    !GpsProcessing::verifyNmeaChecksum("$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,*00", true),
    "invalid checksum should fail"
  );
  require(
    GpsProcessing::verifyNmeaChecksum("$GPRMC,120000.00,A,5001.000,N,01957.000,E,0.10,90.0,250526,,,A", false),
    "checksum-less sentence should pass when checksum is optional"
  );
}

void testGgaParsing() {
  GpsData gps;
  GpsProcessing::processNmeaSentence(
    gps,
    "$GPGGA,123519,4807.038,N,01131.000,E,1,08,0.9,545.4,M,46.9,M,,",
    1000,
    false
  );

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
}

void testRmcAndGsaParsing() {
  GpsData gps;

  GpsProcessing::processNmeaSentence(
    gps,
    "$GPRMC,120001.00,A,5001.000,N,01957.000,E,1.50,90.0,250526,,,A",
    2000,
    false
  );
  require(gps.hasFix, "RMC active sentence should set fix");
  require(gps.locationValid, "RMC should set location valid");
  require(gps.speedValid, "RMC should set speed valid");
  require(gps.courseValid, "RMC should set course valid");
  require(gps.dateValid, "RMC should set date valid");
  require(nearlyEqual(gps.speedKmh, 1.50 * 1.852, 1e-3), "RMC speed km/h mismatch");

  GpsProcessing::processNmeaSentence(
    gps,
    "$GPGSA,A,2,04,05,09,12,24,25,29,31,02,14,18,22,1.8,1.0,1.2",
    2100,
    false
  );
  require(gps.fixTypeValid, "GSA should set fix type valid");
  require(gps.fixType == 2, "GSA should parse 2D fix");
  require(gps.pdopValid && gps.vdopValid, "GSA should set DOP validity");
}

void testSnapshotStates() {
  GpsData gps;

  GpsProcessing::processNmeaSentence(
    gps,
    "$GPGGA,123519,4807.038,N,01131.000,E,1,05,0.9,545.4,M,46.9,M,,",
    1000,
    false
  );
  GpsProcessing::processNmeaSentence(
    gps,
    "$GPGSA,A,2,04,05,09,12,24,25,29,31,02,14,18,22,1.8,1.0,1.2",
    1100,
    false
  );

  GpsValiditySnapshot degraded2d = GpsProcessing::buildGpsSnapshot(gps, 1200, 1800, 6);
  require(degraded2d.diagnosticState == DiagnosticState::Degraded2D, "2D fix should degrade to DEGRADDED_2D");
  require(!degraded2d.usableAltitude, "2D fix should suppress altitude");

  gps.fixType = 3;
  gps.satellitesUsed = 5;
  GpsValiditySnapshot lowSat = GpsProcessing::buildGpsSnapshot(gps, 1300, 1800, 6);
  require(lowSat.diagnosticState == DiagnosticState::DegradedLowSat, "3D fix with low satellites should be degraded");

  gps.satellitesUsed = 7;
  GpsValiditySnapshot ok = GpsProcessing::buildGpsSnapshot(gps, 1400, 1800, 6);
  require(ok.diagnosticState == DiagnosticState::Ok, "3D fix with enough satellites should be OK");

  GpsValiditySnapshot stale = GpsProcessing::buildGpsSnapshot(gps, 4000, 1800, 6);
  require(stale.diagnosticState == DiagnosticState::NoData, "stale data should become NO_DATA");
}

void testNmeaStreamFramer() {
  NmeaStreamFramer::LineAccumulator framer;
  char sentence[144];

  require(
    framer.feed('x', sentence, sizeof(sentence)) == NmeaStreamFramer::FeedResult::None,
    "noise before sentence should be ignored"
  );

  const char *input = "$GPRMC,120001.00,A,5001.000,N,01957.000,E,1.50,90.0,250526,,,A\r\n";
  NmeaStreamFramer::FeedResult result = NmeaStreamFramer::FeedResult::None;

  for (const char *p = input; *p; ++p) {
    result = framer.feed(*p, sentence, sizeof(sentence));
  }

  require(result == NmeaStreamFramer::FeedResult::Complete, "full sentence should complete");
  require(std::string(sentence) == "$GPRMC,120001.00,A,5001.000,N,01957.000,E,1.50,90.0,250526,,,A", "framer output mismatch");

  framer.reset();
  bool sawOverflow = false;
  for (int i = 0; i < 160; ++i) {
    result = framer.feed(i == 0 ? '$' : 'A', sentence, sizeof(sentence));
    if (result == NmeaStreamFramer::FeedResult::Overflow) {
      sawOverflow = true;
    }
  }
  require(sawOverflow, "long sentence should overflow");
}

void testPresentationModel() {
  GpsData gps;

  GpsProcessing::processNmeaSentence(
    gps,
    "$GPGGA,123519,5001.5991,N,01957.2161,E,1,07,0.8,263.1,M,40.0,M,,",
    1000,
    false
  );
  GpsProcessing::processNmeaSentence(
    gps,
    "$GPGSA,A,3,04,05,09,12,24,25,29,31,02,14,18,22,1.6,0.8,1.4",
    1100,
    false
  );

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
  require(!pattern.error && pattern.data && !pattern.warning && pattern.ok, "OK state LED pattern mismatch");
}

}  // namespace

int main() {
  testChecksumValidation();
  testGgaParsing();
  testRmcAndGsaParsing();
  testSnapshotStates();
  testNmeaStreamFramer();
  testPresentationModel();
  std::cout << "Firmware logic tests passed\n";
  return 0;
}
