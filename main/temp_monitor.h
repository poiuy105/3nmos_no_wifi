#ifndef TEMP_MONITOR_H
#define TEMP_MONITOR_H

#include <stdbool.h>
#include "board.h"

#define TEMP_SENSOR_GPIO  PIN_DS18B20   // DS18B20 数据线
#define TEMP_FAULT_VALUE  (-1000.0f)    // 读取故障时的占位温度

// 初始化 DS18B20 并启动温度监测任务（工作模式调用）
void  temp_monitor_start(void);

// 当前是否处于过温提示状态（供 key 任务让出 LED 控制）
bool  temp_monitor_is_overtemp(void);

// 最近一次温度读数（°C），故障返回 TEMP_FAULT_VALUE（供配置页显示）
float temp_monitor_get_temp(void);

// ---- 风扇控制（仅 S3 有 PIN_FAN）----
typedef enum { FAN_MODE_AUTO = 0, FAN_MODE_ON = 1, FAN_MODE_OFF = 2 } fan_mode_t;
void      temp_monitor_fan_set_mode(fan_mode_t mode);   // auto=按阈值自动, on=强制开, off=强制关
fan_mode_t temp_monitor_fan_get_mode(void);
bool      temp_monitor_fan_is_on(void);                  // 当前风扇 GPIO 实际输出

#endif
