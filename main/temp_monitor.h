#ifndef TEMP_MONITOR_H
#define TEMP_MONITOR_H

#include <stdbool.h>

#define TEMP_SENSOR_GPIO  14            // DS18B20 数据线
#define TEMP_FAULT_VALUE  (-1000.0f)    // 读取故障时的占位温度

// 初始化 DS18B20 并启动温度监测任务（工作模式调用）
void  temp_monitor_start(void);

// 当前是否处于过温提示状态（供 key 任务让出 LED 控制）
bool  temp_monitor_is_overtemp(void);

// 最近一次温度读数（°C），故障返回 TEMP_FAULT_VALUE（供配置页显示）
float temp_monitor_get_temp(void);

#endif
