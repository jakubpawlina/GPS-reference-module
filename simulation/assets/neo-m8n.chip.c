#include "wokwi-api.h"

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdarg.h>

#define GPS_SIM_BAUD_RATE 9600U
#define GPS_SIM_SEND_PERIOD_US 1000000U

#define GPS_SIM_OUTPUT_BUFFER_SIZE 768U
#define GPS_SIM_SENTENCE_BODY_SIZE 192U
#define GPS_SIM_COORD_BUFFER_SIZE 16U
#define GPS_SIM_UTC_TIME_BUFFER_SIZE 16U

#define GPS_SIM_MAX_GSA_SATELLITES 12U
#define GPS_SIM_AUTO_PHASE_SECONDS 8ULL
#define GPS_SIM_AUTO_PHASE_COUNT 5ULL

#define GPS_SIM_BASE_LAT_DEG 52.229700
#define GPS_SIM_BASE_LON_DEG 21.012200
#define GPS_SIM_BASE_ALT_M 120.0
#define GPS_SIM_GEOID_SEPARATION_M 34.0
#define GPS_SIM_COORD_JITTER_DEG 0.00000003
#define GPS_SIM_RMC_DATE "260426"

typedef enum {
  GPS_SCENARIO_AUTO = 0,
  GPS_SCENARIO_NO_DATA = 1,
  GPS_SCENARIO_NO_FIX = 2,
  GPS_SCENARIO_FIX_2D = 3,
  GPS_SCENARIO_FIX_3D_LOW_SAT = 4,
  GPS_SCENARIO_REFERENCE_OK = 5
} gps_scenario_t;

typedef enum {
  GPS_FIX_NONE = 0,
  GPS_FIX_2D = 2,
  GPS_FIX_3D = 3
} gps_fix_mode_t;

typedef struct {
  bool transmit_data;
  gps_fix_mode_t fix_mode;
  uint8_t satellites_used;
} gps_sim_state_t;

typedef struct {
  uart_dev_t uart;
  timer_t timer;
  uint32_t scenario_attr;
  uint32_t sequence;
  char output_buffer[GPS_SIM_OUTPUT_BUFFER_SIZE];
} gps_chip_t;

static bool append_format(char *destination, size_t destination_size, const char *format, ...) {
  if (destination == NULL || destination_size == 0U || format == NULL) {
    return false;
  }

  const size_t current_length = strlen(destination);

  if (current_length >= destination_size) {
    return false;
  }

  va_list args;
  va_start(args, format);
  const int written = vsnprintf(
    destination + current_length,
    destination_size - current_length,
    format,
    args
  );
  va_end(args);

  if (written < 0) {
    return false;
  }

  return (size_t)written < (destination_size - current_length);
}

static uint8_t calculate_nmea_checksum(const char *sentence_body) {
  uint8_t checksum = 0U;

  if (sentence_body == NULL) {
    return checksum;
  }

  for (const char *cursor = sentence_body; *cursor != '\0'; cursor++) {
    checksum ^= (uint8_t)(*cursor);
  }

  return checksum;
}

static bool append_nmea_sentence(char *destination, size_t destination_size, const char *sentence_body) {
  if (sentence_body == NULL) {
    return false;
  }

  const uint8_t checksum = calculate_nmea_checksum(sentence_body);

  return append_format(
    destination,
    destination_size,
    "$%s*%02X\r\n",
    sentence_body,
    (unsigned int)checksum
  );
}

static void format_nmea_coordinate(
  double decimal_degrees,
  bool latitude_format,
  char *coordinate,
  size_t coordinate_size,
  char *hemisphere,
  size_t hemisphere_size
) {
  if (coordinate == NULL || coordinate_size == 0U || hemisphere == NULL || hemisphere_size == 0U) {
    return;
  }

  const bool negative = decimal_degrees < 0.0;
  const double absolute_degrees = negative ? -decimal_degrees : decimal_degrees;
  uint32_t whole_degrees = (uint32_t)absolute_degrees;

  const double minutes = (absolute_degrees - (double)whole_degrees) * 60.0;
  uint32_t minute_units = (uint32_t)((minutes * 10000.0) + 0.5);

  if (minute_units >= 600000U) {
    whole_degrees++;
    minute_units -= 600000U;
  }

  const uint32_t minute_whole = minute_units / 10000U;
  const uint32_t minute_fraction = minute_units % 10000U;

  const char hemisphere_char = negative
    ? (latitude_format ? 'S' : 'W')
    : (latitude_format ? 'N' : 'E');

  (void)snprintf(hemisphere, hemisphere_size, "%c", hemisphere_char);

  if (latitude_format) {
    (void)snprintf(
      coordinate,
      coordinate_size,
      "%02u%02u.%04u",
      (unsigned int)whole_degrees,
      (unsigned int)minute_whole,
      (unsigned int)minute_fraction
    );
  } else {
    (void)snprintf(
      coordinate,
      coordinate_size,
      "%03u%02u.%04u",
      (unsigned int)whole_degrees,
      (unsigned int)minute_whole,
      (unsigned int)minute_fraction
    );
  }
}

static void make_utc_time(char *output, size_t output_size) {
  if (output == NULL || output_size == 0U) {
    return;
  }

  const uint64_t sim_seconds = get_sim_nanos() / 1000000000ULL;
  const uint32_t day_seconds = (uint32_t)(sim_seconds % 86400ULL);

  const uint32_t hours = day_seconds / 3600U;
  const uint32_t minutes = (day_seconds % 3600U) / 60U;
  const uint32_t seconds = day_seconds % 60U;

  (void)snprintf(
    output,
    output_size,
    "%02u%02u%02u.00",
    (unsigned int)hours,
    (unsigned int)minutes,
    (unsigned int)seconds
  );
}

static double coordinate_jitter(uint32_t sequence, uint8_t offset) {
  const int32_t pattern = (int32_t)((sequence + (uint32_t)offset) % 5U) - 2;
  return (double)pattern * GPS_SIM_COORD_JITTER_DEG;
}

static gps_sim_state_t make_state(bool transmit_data, gps_fix_mode_t fix_mode, uint8_t satellites_used) {
  gps_sim_state_t state;
  state.transmit_data = transmit_data;
  state.fix_mode = fix_mode;
  state.satellites_used = satellites_used;
  return state;
}

static gps_sim_state_t resolve_auto_demo_state(void) {
  const uint64_t sim_seconds = get_sim_nanos() / 1000000000ULL;
  const uint64_t phase = (sim_seconds / GPS_SIM_AUTO_PHASE_SECONDS) % GPS_SIM_AUTO_PHASE_COUNT;

  switch (phase) {
    case 0ULL:
      return make_state(false, GPS_FIX_NONE, 0U);

    case 1ULL:
      return make_state(true, GPS_FIX_NONE, 3U);

    case 2ULL:
      return make_state(true, GPS_FIX_2D, 4U);

    case 3ULL:
      return make_state(true, GPS_FIX_3D, 5U);

    case 4ULL:
    default:
      return make_state(true, GPS_FIX_3D, 9U);
  }
}

static gps_sim_state_t resolve_selected_state(const gps_chip_t *chip) {
  if (chip == NULL) {
    return make_state(false, GPS_FIX_NONE, 0U);
  }

  const gps_scenario_t scenario = (gps_scenario_t)attr_read(chip->scenario_attr);

  switch (scenario) {
    case GPS_SCENARIO_AUTO:
      return resolve_auto_demo_state();

    case GPS_SCENARIO_NO_DATA:
      return make_state(false, GPS_FIX_NONE, 0U);

    case GPS_SCENARIO_NO_FIX:
      return make_state(true, GPS_FIX_NONE, 3U);

    case GPS_SCENARIO_FIX_2D:
      return make_state(true, GPS_FIX_2D, 4U);

    case GPS_SCENARIO_FIX_3D_LOW_SAT:
      return make_state(true, GPS_FIX_3D, 5U);

    case GPS_SCENARIO_REFERENCE_OK:
    default:
      return make_state(true, GPS_FIX_3D, 9U);
  }
}

static uint8_t clamp_satellite_count(uint8_t satellites_used) {
  if (satellites_used > GPS_SIM_MAX_GSA_SATELLITES) {
    return GPS_SIM_MAX_GSA_SATELLITES;
  }

  return satellites_used;
}

static void append_gga_sentence(char *output, size_t output_size, gps_sim_state_t state, uint32_t sequence) {
  char body[GPS_SIM_SENTENCE_BODY_SIZE] = "";
  char utc_time[GPS_SIM_UTC_TIME_BUFFER_SIZE] = "";
  char latitude[GPS_SIM_COORD_BUFFER_SIZE] = "";
  char longitude[GPS_SIM_COORD_BUFFER_SIZE] = "";
  char latitude_hemisphere[2] = "";
  char longitude_hemisphere[2] = "";

  make_utc_time(utc_time, sizeof(utc_time));

  const bool has_fix = state.fix_mode != GPS_FIX_NONE;
  const double latitude_value = GPS_SIM_BASE_LAT_DEG + coordinate_jitter(sequence, 0U);
  const double longitude_value = GPS_SIM_BASE_LON_DEG + coordinate_jitter(sequence, 2U);

  format_nmea_coordinate(latitude_value, true, latitude, sizeof(latitude), latitude_hemisphere, sizeof(latitude_hemisphere));
  format_nmea_coordinate(longitude_value, false, longitude, sizeof(longitude), longitude_hemisphere, sizeof(longitude_hemisphere));

  const unsigned int fix_quality = has_fix ? 1U : 0U;
  const double hdop = has_fix ? (state.fix_mode == GPS_FIX_2D ? 1.7 : 0.8) : 99.9;
  const double altitude = has_fix ? GPS_SIM_BASE_ALT_M : 0.0;
  const unsigned int satellites_used = (unsigned int)state.satellites_used;

  (void)snprintf(
    body,
    sizeof(body),
    "GNGGA,%s,%s,%s,%s,%s,%u,%02u,%.1f,%.1f,M,%.1f,M,,",
    utc_time,
    has_fix ? latitude : "",
    has_fix ? latitude_hemisphere : "",
    has_fix ? longitude : "",
    has_fix ? longitude_hemisphere : "",
    fix_quality,
    satellites_used,
    hdop,
    altitude,
    GPS_SIM_GEOID_SEPARATION_M
  );

  (void)append_nmea_sentence(output, output_size, body);
}

static void append_rmc_sentence(char *output, size_t output_size, gps_sim_state_t state, uint32_t sequence) {
  char body[GPS_SIM_SENTENCE_BODY_SIZE] = "";
  char utc_time[GPS_SIM_UTC_TIME_BUFFER_SIZE] = "";
  char latitude[GPS_SIM_COORD_BUFFER_SIZE] = "";
  char longitude[GPS_SIM_COORD_BUFFER_SIZE] = "";
  char latitude_hemisphere[2] = "";
  char longitude_hemisphere[2] = "";

  make_utc_time(utc_time, sizeof(utc_time));

  const bool has_fix = state.fix_mode != GPS_FIX_NONE;
  const double latitude_value = GPS_SIM_BASE_LAT_DEG + coordinate_jitter(sequence, 1U);
  const double longitude_value = GPS_SIM_BASE_LON_DEG + coordinate_jitter(sequence, 3U);

  format_nmea_coordinate(latitude_value, true, latitude, sizeof(latitude), latitude_hemisphere, sizeof(latitude_hemisphere));
  format_nmea_coordinate(longitude_value, false, longitude, sizeof(longitude), longitude_hemisphere, sizeof(longitude_hemisphere));

  (void)snprintf(
    body,
    sizeof(body),
    "GNRMC,%s,%c,%s,%s,%s,%s,0.02,054.7,%s,,,A",
    utc_time,
    has_fix ? 'A' : 'V',
    has_fix ? latitude : "",
    has_fix ? latitude_hemisphere : "",
    has_fix ? longitude : "",
    has_fix ? longitude_hemisphere : "",
    GPS_SIM_RMC_DATE
  );

  (void)append_nmea_sentence(output, output_size, body);
}

static void append_gsa_sentence(char *output, size_t output_size, gps_sim_state_t state) {
  char body[GPS_SIM_SENTENCE_BODY_SIZE] = "";
  const bool has_fix = state.fix_mode != GPS_FIX_NONE;
  const uint8_t satellites_used = clamp_satellite_count(state.satellites_used);

  char satellite_fields[(GPS_SIM_MAX_GSA_SATELLITES * 4U) + 1U] = "";

  if (has_fix) {
    for (uint8_t index = 0U; index < satellites_used; index++) {
      const unsigned int satellite_id = 10U + (unsigned int)index;
      (void)append_format(
        satellite_fields,
        sizeof(satellite_fields),
        "%02u,",
        satellite_id
      );
    }
  }

  for (uint8_t index = satellites_used; index < GPS_SIM_MAX_GSA_SATELLITES; index++) {
    (void)append_format(satellite_fields, sizeof(satellite_fields), ",");
  }

  const unsigned int fix_type = has_fix ? (unsigned int)state.fix_mode : 1U;
  const double pdop = has_fix ? (state.fix_mode == GPS_FIX_2D ? 2.8 : 1.6) : 99.9;
  const double hdop = has_fix ? (state.fix_mode == GPS_FIX_2D ? 1.7 : 0.8) : 99.9;
  const double vdop = has_fix ? (state.fix_mode == GPS_FIX_2D ? 9.9 : 1.4) : 99.9;

  (void)snprintf(
    body,
    sizeof(body),
    "GNGSA,A,%u,%s%.1f,%.1f,%.1f",
    fix_type,
    satellite_fields,
    pdop,
    hdop,
    vdop
  );

  (void)append_nmea_sentence(output, output_size, body);
}

static void send_nmea_frame(gps_chip_t *chip) {
  if (chip == NULL) {
    return;
  }

  const gps_sim_state_t state = resolve_selected_state(chip);

  if (!state.transmit_data) {
    return;
  }

  chip->output_buffer[0] = '\0';

  append_gga_sentence(chip->output_buffer, sizeof(chip->output_buffer), state, chip->sequence);
  append_rmc_sentence(chip->output_buffer, sizeof(chip->output_buffer), state, chip->sequence);
  append_gsa_sentence(chip->output_buffer, sizeof(chip->output_buffer), state);

  uart_write(chip->uart, (uint8_t *)chip->output_buffer, strlen(chip->output_buffer));
  chip->sequence++;
}

static void gps_timer_callback(void *user_data) {
  gps_chip_t *chip = (gps_chip_t *)user_data;
  send_nmea_frame(chip);
  timer_start(chip->timer, GPS_SIM_SEND_PERIOD_US, false);
}

void chip_init(void) {
  gps_chip_t *chip = malloc(sizeof(gps_chip_t));
  if (chip == NULL) {
    return;
  }

  memset(chip, 0, sizeof(gps_chip_t));

  const uart_config_t uart_config = {
    .tx = pin_init("TX", INPUT_PULLUP),
    .rx = pin_init("RX", INPUT),
    .baud_rate = GPS_SIM_BAUD_RATE,
    .user_data = chip,
  };
  chip->uart = uart_init(&uart_config);
  chip->timer = timer_init(&(timer_config_t) {
    .callback = gps_timer_callback,
    .user_data = chip
  });
  chip->scenario_attr = attr_init("scenario", 0U);
  chip->sequence = 0U;

  timer_start(chip->timer, GPS_SIM_SEND_PERIOD_US, false);
}
