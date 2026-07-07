#ifndef INPUT_SIG_H
#define INPUT_SIG_H

#include <stdbool.h>

// 配置输入信号引脚 GPIO（按 nvs 的 in_pin）
void input_sig_init(void);

// 立即读取一次输入信号的"逻辑电平"（已应用 in_inv 反向）
bool input_sig_read_logical(void);

// 启动 5ms 轮询任务，电平稳定变化后驱动 pwm_ctrl 切换
void input_sig_start(void);

#endif
