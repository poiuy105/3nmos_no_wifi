#ifndef KEY_H
#define KEY_H

#include <stdbool.h>

// 启动按键常驻任务（工作模式调用）：短按 LED 反馈，长按 5s 重启进配置
void key_task_start(void);

// 启动时调用一次：消费"长按触发进配置"的 RTC 标志
bool key_consume_enter_config(void);

// 置 RTC 标志并立即重启，进入 WiFi 配置模式（供 CLI 的 config-mode 命令复用）
void key_enter_config_mode(void);

#endif
