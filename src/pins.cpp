#include <Arduino.h>
#include <board_config.h>
#include <config.h>
#ifndef BOARD_TRMNL_X
#endif

void pins_init(void) {
  gpio_config_t io_conf = {};
  io_conf.intr_type = GPIO_INTR_DISABLE;
  io_conf.mode = GPIO_MODE_INPUT;
#ifdef BOARD_TRMNL_X
  io_conf.pin_bit_mask = (1ULL << PIN_INTERRUPT);
#elif defined(BOARD_XTEINK_X4_CLASSIC)
  // Every wake key is an active-low input with a pull-up: Power plus the four
  // bottom keys, which wake the unit for a manual fetch + hard refresh.
  io_conf.pin_bit_mask = X4C_WAKE_KEY_MASK;
#else
  io_conf.pin_bit_mask = (1ULL << pDevice->interrupt_pin);
#endif
  io_conf.pull_down_en = GPIO_PULLDOWN_DISABLE;
  io_conf.pull_up_en = GPIO_PULLUP_ENABLE;
  gpio_config(&io_conf);
}
