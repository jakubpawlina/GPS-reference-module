/**
 * @file gps_processing.h
 * @brief Public API for NMEA parsing, GPS state accumulation, and diagnostics.
 */
#pragma once

#include <stddef.h>
#include <stdint.h>

/**
 * @brief Pure firmware logic for NMEA parsing and diagnostic-state evaluation.
 *
 * This module is intentionally free of Arduino runtime dependencies so it can
 * be compiled and tested on the host with a normal C++ compiler.
 */
namespace GpsProcessing {

/** @brief High-level diagnostic state exposed by the firmware.
 *
 * States are ordered from worst (NoData) to best (Ok).  The JSON API and
 * OLED display each have their own string representations.
 */
enum class DiagnosticState : uint8_t {
  NoData,         ///< No NMEA data received from the GPS receiver.
  NoFix,          ///< NMEA arriving but no position fix acquired.
  Degraded2D,     ///< Fix acquired but only 2D (no altitude).
  DegradedLowSat, ///< 3D fix but below the minimum satellite threshold.
  Ok              ///< 3D fix with sufficient satellites — reference quality.
};

/** @brief Supported NMEA sentence types handled by the parser. */
enum class NmeaSentenceType : uint8_t {
  Unknown, ///< Unrecognised or unsupported sentence.
  Gga,     ///< GGA — fix data, satellites, altitude, HDOP.
  Gsa,     ///< GSA — fix type (2D/3D), DOP values.
  Rmc      ///< RMC — position, speed, course, date.
};

/**
 * @brief Accumulated GPS state built from recently parsed NMEA sentences.
 *
 * Fields are updated incrementally as GGA, GSA, and RMC sentences arrive.
 * Multiple sentence types may set the same field (e.g. hasFix); the most
 * recent write wins.  Timestamps record when each sentence type was last
 * processed so freshness can be evaluated.
 */
struct GpsData {
  /// @name Validity flags
  /// @{
  bool nmeaValid = false;     ///< True after the first accepted NMEA sentence.
  bool hasFix = false;        ///< True when any sentence reports a position fix.
  bool locationValid = false; ///< True when lat/lon have been parsed successfully.
  bool altitudeValid = false; ///< True when altitude is available (GGA field 9).
  bool geoidValid = false;    ///< True when geoid separation is available (GGA field 11).
  bool fixTypeValid = false;  ///< True after the first GSA sentence sets fixType.
  bool hdopValid = false;     ///< True when HDOP is available (GGA or GSA).
  bool pdopValid = false;     ///< True when PDOP is available (GSA field 15).
  bool vdopValid = false;     ///< True when VDOP is available (GSA field 17).
  bool speedValid = false;    ///< True when speed is available (RMC field 7).
  bool courseValid = false;   ///< True when course-over-ground is available (RMC field 8).
  bool timeValid = false;     ///< True when UTC time has been parsed (GGA or RMC).
  bool dateValid = false;     ///< True when UTC date has been parsed (RMC field 9).
  /// @}

  /// @name Fix information
  /// @{
  uint8_t fixType = 0;        ///< GSA fix type: 2 = 2D, 3 = 3D, 0 = none.
  uint8_t fixQuality = 0;     ///< GGA fix quality indicator (0 = invalid, 1 = GPS, 2 = DGPS).
  uint8_t satellitesUsed = 0; ///< Number of satellites used in the solution (GGA field 7).
  /// @}

  /// @name Position and altitude
  /// @{
  double latitude = 0.0;         ///< Decimal degrees WGS-84, negative for south.
  double longitude = 0.0;        ///< Decimal degrees WGS-84, negative for west.
  double altitudeM = 0.0;        ///< Altitude above mean sea level in metres.
  double geoidSeparationM = 0.0; ///< Geoid-to-ellipsoid separation in metres.
  /// @}

  /// @name Dilution of precision
  /// @{
  double hdop = 0.0; ///< Horizontal dilution of precision.
  double pdop = 0.0; ///< Position (3D) dilution of precision.
  double vdop = 0.0; ///< Vertical dilution of precision.
  /// @}

  /// @name Motion
  /// @{
  double speedKnots = 0.0; ///< Speed over ground in knots (RMC field 7).
  double speedKmh = 0.0;   ///< Speed over ground in km/h (derived from knots).
  double courseDeg = 0.0;  ///< Course over ground in degrees true (RMC field 8).
  /// @}

  /// @name Time
  /// @{
  char utcTime[16] = ""; ///< UTC time as HHMMSS.ss from GGA or RMC.
  char utcDate[8] = "";  ///< UTC date as DDMMYY from RMC.
  /// @}

  /// @name Timestamps (millis)
  /// @{
  uint32_t lastNmeaMs = 0; ///< millis() when the last accepted sentence arrived.
  uint32_t lastGgaMs = 0;  ///< millis() when the last GGA was processed.
  uint32_t lastGsaMs = 0;  ///< millis() when the last GSA was processed.
  uint32_t lastRmcMs = 0;  ///< millis() when the last RMC was processed.
  /// @}

  /// @name Counters
  /// @{
  uint32_t acceptedSentenceCount = 0; ///< Sentences that passed checksum and were parsed.
  uint32_t rawSentenceCount = 0;      ///< Total sentences received (including rejected).
  uint32_t checksumErrorCount = 0;    ///< Sentences rejected due to checksum mismatch.
  uint32_t bufferOverflowCount = 0;   ///< Sentences dropped because the framer buffer overflowed.
  /// @}
};

/**
 * @brief Time-relative interpretation of GpsData for a single loop tick.
 *
 * Built by buildGpsSnapshot() once per tick, this struct captures which
 * data sources are fresh (arrived within the timeout window) and derives
 * composite usability flags.  All consumers within one tick share the same
 * snapshot so freshness decisions are coherent.
 */
struct GpsValiditySnapshot {
  uint32_t nowMs = 0; ///< The millis() value at snapshot creation.

  /// @name Per-sentence freshness
  /// @{
  bool freshNmea = false; ///< Any accepted NMEA arrived within the timeout.
  bool freshGga = false;  ///< A GGA sentence arrived within the timeout.
  bool freshGsa = false;  ///< A GSA sentence arrived within the timeout.
  bool freshRmc = false;  ///< An RMC sentence arrived within the timeout.
  /// @}

  /// @name Composite usability
  /// @{
  bool freshPositionSource =
      false;                   ///< At least one position-bearing sentence (GGA or RMC) is fresh.
  bool freshFixSource = false; ///< At least one fix-bearing sentence (GGA, GSA, or RMC) is fresh.
  bool currentFix = false;     ///< A fresh fix source reports hasFix == true.
  bool usablePosition = false; ///< Fresh position + current fix + valid coordinates.
  bool usableAltitude = false; ///< Usable position with a valid 3D altitude.
  bool usableHdop = false;     ///< A fresh HDOP value is available.
  /// @}

  /// @name Sentence ages (milliseconds, 0 when not fresh)
  /// @{
  uint32_t nmeaAgeMs = 0; ///< Time since the last accepted NMEA sentence.
  uint32_t ggaAgeMs = 0;  ///< Time since the last GGA sentence.
  uint32_t gsaAgeMs = 0;  ///< Time since the last GSA sentence.
  uint32_t rmcAgeMs = 0;  ///< Time since the last RMC sentence.
  /// @}

  DiagnosticState diagnosticState = DiagnosticState::NoData; ///< Overall diagnostic classification.
};

/** @brief Validate a NMEA checksum, optionally accepting checksum-less input. */
bool verifyNmeaChecksum(const char *sentence, bool requireChecksum);
/** @brief Detect the handled NMEA sentence type directly from raw text. */
NmeaSentenceType detectSentenceTypeFromRaw(const char *sentence);
/** @brief Convert a sentence type enum to protocol text. */
const char *sentenceTypeToText(NmeaSentenceType type);
/** @brief Parse one accepted sentence and merge its contents into gps.
 *
 * When checksumAlreadyVerified is true the caller has already validated the
 * checksum — the function skips re-verification and treats the sentence as
 * accepted.  Pass false to perform the checksum check internally.
 */
void processNmeaSentence(GpsData &gps, const char *sentence, uint32_t nowMs, bool requireChecksum,
                         bool checksumAlreadyVerified = false);
/** @brief Build a time-relative validity snapshot used by output and LEDs. */
GpsValiditySnapshot buildGpsSnapshot(const GpsData &gps, uint32_t nowMs, uint32_t gpsDataTimeoutMs,
                                     uint8_t minOkSatellites);
/** @brief Convert a diagnostic state to the JSON API representation. */
const char *diagnosticStateToJson(DiagnosticState state);
/** @brief Convert a diagnostic state to the compact OLED label. */
const char *diagnosticStateToDisplay(DiagnosticState state);
/** @brief Convert fix information to the JSON fix type text. */
const char *fixTypeToText(const GpsData &gps, const GpsValiditySnapshot &snapshot);
/** @brief Convert fix information to the compact OLED label. */
const char *fixTypeToDisplay(const GpsData &gps, const GpsValiditySnapshot &snapshot);

} // namespace GpsProcessing
