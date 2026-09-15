#ifndef MAIN_H
#define MAIN_H

#include <Arduino.h>

#define freq_to_timerval(x) ((uint32_t)(x))
#define NUM_ELEM(x) (sizeof(x) / sizeof((x)[0]))

typedef struct IrCode {
  uint32_t timer_val;
  uint8_t numpairs;
  uint8_t bitcompression;
  const uint16_t *times;
  const uint8_t *codes;
} IrCode;
#endif