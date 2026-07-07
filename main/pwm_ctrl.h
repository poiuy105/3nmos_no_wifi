#ifndef PWM_CTRL_H
#define PWM_CTRL_H

#include <stdbool.h>

// 初始化 3 路 LEDC（含 fade 中断服务）
void pwm_ctrl_init(void);

// 应用输入信号对应的 PWM 状态。
// logical_high=true 输出"高电平态"参数，false 输出"低电平态"参数。
// instant=true 时不做 fade（上电立即生效），false 时按 t_rise/t_fall 平滑切换占空比。
void pwm_ctrl_apply_state(bool logical_high, bool instant);

#endif
