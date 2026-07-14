#ifndef CLI_H
#define CLI_H

#include <stdbool.h>
#include "esp_log.h"

// 启动串口 CLI 任务（UART0，115200）。
//   work_mode=true  -> WORK 模式，全部命令可用
//   work_mode=false -> CONFIG 模式，work_only 命令（pwm/set）被拒绝
void cli_start(bool work_mode);

// 当前 CLI 是否运行在 WORK 模式
bool cli_in_work_mode(void);

// 事件驱动 debug 开关读取（供 CLI_DEBUG 宏使用）
bool cli_debug_on(void);

// 事件驱动详细日志：cli_debug_on() 为假时短路，不构造参数、零开销。
// 各模块在关键事件点调用，TAG 用本模块自己的，保持 ESP_LOG 前缀一致。
#define CLI_DEBUG(tag, fmt, ...) \
    do { if (cli_debug_on()) ESP_LOGI(tag, "[EVT] " fmt, ##__VA_ARGS__); } while (0)

#endif // CLI_H
