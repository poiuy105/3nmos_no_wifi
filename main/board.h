#ifndef BOARD_H
#define BOARD_H

#include "driver/gpio.h"

/* 双芯片引脚映射：按编译目标自动选择 ESP32-S3 或 ESP32-C3 的引脚 */

#if CONFIG_IDF_TARGET_ESP32S3
  #define PIN_KEY     GPIO_NUM_0
  #define PIN_LED     GPIO_NUM_15
  #define PIN_IN1     10
  #define PIN_IN2     9
  #define PIN_PWM1    47          // 唯一 PWM（原 PWM3 引脚）
  #define PIN_DS18B20 48          // DS18B20：原 PWM1 引脚（GPIO14 是 SPI flash SPIHD，不可用）
  #define PIN_FAN     18          // 散热风扇（高电平=开），仅 S3
#elif CONFIG_IDF_TARGET_ESP32C3
  #define PIN_KEY     GPIO_NUM_9
  #define PIN_LED     GPIO_NUM_3
  #define PIN_IN1     6
  #define PIN_IN2     7
  #define PIN_PWM1    10          // 唯一 PWM（原 PWM3 引脚）
  #define PIN_DS18B20 2
  // C3 不接风扇（仅 S3 有 PIN_FAN）
#else
  #error "仅支持 ESP32-S3 / ESP32-C3"
#endif

#endif
