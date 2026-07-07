#ifndef APP_H
#define APP_H

#include "driver/gpio.h"

#define LED_PIN  GPIO_NUM_15   // 低电平点亮
#define KEY_PIN  GPIO_NUM_0    // 内部上拉，按下拉低

void app_main(void);

#endif
