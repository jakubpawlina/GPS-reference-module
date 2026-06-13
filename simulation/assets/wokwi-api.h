#ifndef WOKWI_API_H
#define WOKWI_API_H

#include <stdbool.h>
#include <stdint.h>

enum pin_mode {
  INPUT = 0,
  OUTPUT = 1,
  INPUT_PULLUP = 2,
  INPUT_PULLDOWN = 3,
  ANALOG = 4,
  OUTPUT_LOW = 16,
  OUTPUT_HIGH = 17,
};

int __attribute__((export_name("__wokwi_api_version_1")))
__attribute__((weak)) __wokwi_api_version_1(void) {
  return 1;
}

typedef int32_t pin_t;

extern __attribute__((export_name("chipInit"))) void chip_init(void);
extern __attribute__((import_name("pinInit"))) pin_t pin_init(
  const char *name,
  uint32_t mode
);

extern __attribute__((import_name("attrInit"))) uint32_t attr_init(
  const char *name,
  uint32_t default_value
);
extern __attribute__((import_name("attrRead"))) uint32_t attr_read(
  uint32_t attr_id
);

typedef struct {
  void *user_data;
  pin_t rx;
  pin_t tx;
  uint32_t baud_rate;
  void (*rx_data)(void *user_data, uint8_t byte);
  void (*write_done)(void *user_data);
  uint32_t reserved[8];
} uart_config_t;

typedef uint32_t uart_dev_t;

extern __attribute__((import_name("uartInit"))) uart_dev_t uart_init(
  const uart_config_t *config
);
extern __attribute__((import_name("uartWrite"))) bool uart_write(
  uart_dev_t uart,
  uint8_t *buffer,
  uint32_t count
);

typedef struct {
  void *user_data;
  void (*callback)(void *user_data);
  uint32_t reserved[8];
} timer_config_t;

typedef uint32_t timer_t;

extern __attribute__((import_name("timerInit"))) timer_t timer_init(
  const timer_config_t *config
);
extern __attribute__((import_name("timerStart"))) void timer_start(
  timer_t timer,
  uint32_t micros,
  bool repeat
);
extern __attribute__((import_name("getSimNanos"))) double get_sim_nanos_d(void);

static uint64_t get_sim_nanos(void) {
  return (uint64_t)get_sim_nanos_d();
}

#endif
