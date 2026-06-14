#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WOKWI_API_H

enum pin_mode {
  INPUT = 0,
  OUTPUT = 1,
  INPUT_PULLUP = 2,
  INPUT_PULLDOWN = 3,
};

typedef int32_t pin_t;
typedef uint32_t uart_dev_t;
typedef uint32_t timer_t;

typedef struct {
  void *user_data;
  pin_t rx;
  pin_t tx;
  uint32_t baud_rate;
  void (*rx_data)(void *user_data, uint8_t byte);
  void (*write_done)(void *user_data);
  uint32_t reserved[8];
} uart_config_t;

typedef struct {
  void *user_data;
  void (*callback)(void *user_data);
  uint32_t reserved[8];
} timer_config_t;

static uint32_t selected_scenario;
static uint64_t simulated_nanos;
static unsigned int uart_write_count;
static char uart_output[2048];
static uart_config_t initialized_uart;
static timer_config_t initialized_timer;
static uint32_t last_timer_period;

static pin_t pin_init(const char *name, uint32_t mode);
static uint32_t attr_init(const char *name, uint32_t default_value);
static uint32_t attr_read(uint32_t attr_id);
static uart_dev_t uart_init(const uart_config_t *config);
static bool uart_write(uart_dev_t uart, uint8_t *buffer, uint32_t count);
static timer_t timer_init(const timer_config_t *config);
static void timer_start(timer_t timer, uint32_t micros, bool repeat);
static uint64_t get_sim_nanos(void);

#define chip_init chip_init_under_test
#include "../../simulation/assets/neo-m8n.chip.c"
#undef chip_init

static void require_condition(bool condition, const char *message) {
  if (!condition) {
    fprintf(stderr, "FAILED: %s\n", message);
    exit(1);
  }
}

static pin_t pin_init(const char *name, uint32_t mode) {
  if (strcmp(name, "TX") == 0) {
    require_condition(mode == INPUT_PULLUP, "TX pin mode mismatch");
    return 1;
  }
  if (strcmp(name, "RX") == 0) {
    require_condition(mode == INPUT, "RX pin mode mismatch");
    return 2;
  }
  return -1;
}

static uint32_t attr_init(const char *name, uint32_t default_value) {
  require_condition(strcmp(name, "scenario") == 0, "scenario attribute name mismatch");
  require_condition(default_value == 0, "scenario default mismatch");
  return 7;
}

static uint32_t attr_read(uint32_t attr_id) {
  require_condition(attr_id == 7, "scenario attribute id mismatch");
  return selected_scenario;
}

static uart_dev_t uart_init(const uart_config_t *config) {
  initialized_uart = *config;
  return 11;
}

static bool uart_write(uart_dev_t uart, uint8_t *buffer, uint32_t count) {
  require_condition(uart == 11, "UART device mismatch");
  require_condition(count < sizeof(uart_output), "UART output exceeded test buffer");
  memcpy(uart_output, buffer, count);
  uart_output[count] = '\0';
  uart_write_count++;
  return true;
}

static timer_t timer_init(const timer_config_t *config) {
  initialized_timer = *config;
  return 13;
}

static void timer_start(timer_t timer, uint32_t micros, bool repeat) {
  require_condition(timer == 13, "timer device mismatch");
  require_condition(!repeat, "GPS timer should be one-shot");
  last_timer_period = micros;
}

static uint64_t get_sim_nanos(void) {
  return simulated_nanos;
}

static unsigned int parse_hex(char c) {
  if (c >= '0' && c <= '9') return (unsigned int)(c - '0');
  if (c >= 'A' && c <= 'F') return (unsigned int)(c - 'A' + 10);
  if (c >= 'a' && c <= 'f') return (unsigned int)(c - 'a' + 10);
  return 255;
}

static void require_valid_checksums(const char *frame) {
  const char *line = frame;
  unsigned int sentence_count = 0;

  while (*line) {
    require_condition(*line == '$', "NMEA sentence must start with $");
    const char *star = strchr(line, '*');
    require_condition(star != NULL, "NMEA sentence must contain checksum");
    require_condition(star[1] != '\0' && star[2] != '\0', "NMEA checksum must contain two digits");

    uint8_t calculated = 0;
    for (const char *cursor = line + 1; cursor < star; cursor++) {
      calculated ^= (uint8_t)*cursor;
    }

    const unsigned int high = parse_hex(star[1]);
    const unsigned int low = parse_hex(star[2]);
    require_condition(high < 16 && low < 16, "NMEA checksum must be hexadecimal");
    require_condition(calculated == (uint8_t)((high << 4) | low), "NMEA checksum mismatch");
    require_condition(star[3] == '\r' && star[4] == '\n', "NMEA sentence must end with CRLF");

    line = star + 5;
    sentence_count++;
  }

  require_condition(sentence_count == 3, "GPS frame should contain GGA, RMC, and GSA");
}

static void reset_capture(void) {
  uart_write_count = 0;
  uart_output[0] = '\0';
}

/*
 * Purpose: Validate the custom chip's contract with the Wokwi runtime.
 * Setup: Initialize the chip through stubbed pin, attribute, UART, and timer APIs.
 * Verifies: Pin modes, 9600-baud UART, callback registration, and timer period.
 */
static void test_initialization(void) {
  chip_init_under_test();
  require_condition(initialized_uart.tx == 1, "initialized UART TX mismatch");
  require_condition(initialized_uart.rx == 2, "initialized UART RX mismatch");
  require_condition(initialized_uart.baud_rate == GPS_SIM_BAUD_RATE, "initialized UART baud mismatch");
  require_condition(initialized_timer.callback != NULL, "timer callback should be configured");
  require_condition(last_timer_period == GPS_SIM_SEND_PERIOD_US, "initial timer period mismatch");
}

/*
 * Helper for data-producing manual scenarios.
 * It freezes simulated time, emits one frame, checks scenario-specific fragments,
 * and validates all three NMEA checksums and CRLF terminators.
 */
static void test_scenario(
  uint32_t scenario,
  const char *gga_fragment,
  const char *rmc_fragment,
  const char *gsa_fragment
) {
  gps_chip_t chip = {0};
  chip.uart = 11;
  chip.timer = 13;
  chip.scenario_attr = 7;

  selected_scenario = scenario;
  simulated_nanos = 3661ULL * 1000000000ULL;
  reset_capture();
  send_nmea_frame(&chip);

  require_condition(uart_write_count == 1, "data scenario should emit one UART frame");
  require_condition(strstr(uart_output, gga_fragment) != NULL, "GGA scenario output mismatch");
  require_condition(strstr(uart_output, rmc_fragment) != NULL, "RMC scenario output mismatch");
  require_condition(strstr(uart_output, gsa_fragment) != NULL, "GSA scenario output mismatch");
  require_valid_checksums(uart_output);
}

/*
 * Purpose: Validate every manually selectable GPS scenario exposed in Wokwi.
 * Setup: Select no-data, no-fix, 2D, low-satellite 3D, and reference-OK states.
 * Verifies: No-data stays silent; data states emit matching GGA/RMC/GSA frames.
 */
static void test_scenarios(void) {
  gps_chip_t chip = {0};
  chip.uart = 11;
  chip.timer = 13;
  chip.scenario_attr = 7;

  selected_scenario = GPS_SCENARIO_NO_DATA;
  reset_capture();
  send_nmea_frame(&chip);
  require_condition(uart_write_count == 0, "no-data scenario should not emit UART data");

  test_scenario(GPS_SCENARIO_NO_FIX, "GNGGA,010101.00,,,,,0,03,99.9", "GNRMC,010101.00,V", "GNGSA,A,1,");
  test_scenario(GPS_SCENARIO_FIX_2D, "GNGGA,010101.00,5213.7820,N,02100.7320,E,1,04,1.7", "GNRMC,010101.00,A", "GNGSA,A,2,");
  test_scenario(GPS_SCENARIO_FIX_3D_LOW_SAT, "GNGGA,010101.00,5213.7820,N,02100.7320,E,1,05,0.8", "GNRMC,010101.00,A", "GNGSA,A,3,");
  test_scenario(GPS_SCENARIO_REFERENCE_OK, "GNGGA,010101.00,5213.7820,N,02100.7320,E,1,09,0.8", "GNRMC,010101.00,A", "GNGSA,A,3,");
}

/*
 * Purpose: Protect the timing and ordering of the visual automatic demonstration.
 * Setup: Sample the resolver at the start of each eight-second phase.
 * Verifies: The five phases deterministically progress from no data to reference OK.
 */
static void test_auto_demo(void) {
  const gps_sim_state_t expected[] = {
    {false, GPS_FIX_NONE, 0},
    {true, GPS_FIX_NONE, 3},
    {true, GPS_FIX_2D, 4},
    {true, GPS_FIX_3D, 5},
    {true, GPS_FIX_3D, 9},
  };

  for (uint64_t phase = 0; phase < GPS_SIM_AUTO_PHASE_COUNT; phase++) {
    simulated_nanos = phase * GPS_SIM_AUTO_PHASE_SECONDS * 1000000000ULL;
    const gps_sim_state_t actual = resolve_auto_demo_state();
    require_condition(actual.transmit_data == expected[phase].transmit_data, "auto demo transmit mismatch");
    require_condition(actual.fix_mode == expected[phase].fix_mode, "auto demo fix mode mismatch");
    require_condition(actual.satellites_used == expected[phase].satellites_used, "auto demo satellite mismatch");
  }
}

int main(void) {
  test_initialization();
  puts(
    "PASS\tCustom chip initialization and timer configuration\t"
    "Configures UART pins, 9600 baud, scenario control, and one-second timer."
  );
  test_scenarios();
  puts(
    "PASS\tManual GPS scenarios and NMEA checksums\t"
    "Emits the expected GGA/RMC/GSA frame with valid checksums for each state."
  );
  test_auto_demo();
  puts(
    "PASS\tAutomatic five-phase GPS demonstration\t"
    "Cycles deterministically through no data, no fix, 2D, low-sat, and OK."
  );
  return 0;
}
