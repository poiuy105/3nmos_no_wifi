#ifndef KEY_H
#define KEY_H

#include <stdbool.h>

// 启动按键常驻任务（工作模式调用）：短按 LED 反馈，长按 5s 重启进配置
void key_task_start(void);

// 启动时调用一次：消费"长按触发进配置"的 RTC 标志
bool key_consume_enter_config(void);

#endif
