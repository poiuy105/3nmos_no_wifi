#ifndef BOARD_H
#define BOARD_H

#include "driver/gpio.h"

/* 双芯片引脚映射：按编译目标自动选择 ESP32-S3 或 ESP32-C3 的引脚 */

#if CONFIG_IDF_TARGET_ESP32S3
  #define PIN_KEY     GPIO_NUM_0
  #define PIN_LED     GPIO_NUM_15
  #define PIN_IN1     10
  #define PIN_IN2     9
  #define PIN_PWM1    48
  #define PIN_PWM2    21
  #define PIN_PWM3    47
  #define PIN_DS18B20 14      /* 注：需求写15，但 GPIO15=LED，故沿用 GPIO14 */
#elif CONFIG_IDF_TARGET_ESP32C3
  #define PIN_KEY     GPIO_NUM_9
  #define PIN_LED     GPIO_NUM_3
  #define PIN_IN1     6
  #define PIN_IN2     7
  #define PIN_PWM1    4
  #define PIN_PWM2    5
  #define PIN_PWM3    10
  #define PIN_DS18B20 2
#else
  #error "仅支持 ESP32-S3 / ESP32-C3"
#endif

#endif
