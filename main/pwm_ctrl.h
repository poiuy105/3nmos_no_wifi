#ifndef PWM_CTRL_H
#define PWM_CTRL_H

#include <stdbool.h>
#include <stdint.h>

// 初始化 3 路 LEDC
void pwm_ctrl_init(void);

// 应用输入信号对应的 PWM 状态。
// logical_high=true 输出"高电平态"参数，false 输出"低电平态"参数。
// instant=true 时不做 fade（上电立即生效），false 时按 t_rise/t_fall 平滑切换占空比。
void pwm_ctrl_apply_state(bool logical_high, bool instant);

// 过温提示：on=true 时把 3 路切到 5Hz/50% 方波；off 时由调用方 pwm_ctrl_apply_state() 恢复
void pwm_ctrl_overtemp_alert(bool on);

// 运行时设置单通道输出（不写 NVS）。
//   ch            : 0..PWM_CH_CNT-1
//   on            : false=该路逻辑 0%（受 pwm_inv）；true=按 state/override 输出
//   state         : PWM_STATE_LO / PWM_STATE_HI（on=false 时忽略）
//   freq_override : 0=用全局 pwm_freq[ch][state]；>0=临时用此频率
//   duty_override : 0xFFFF=用全局 pwm_duty[ch][state]；其它=临时用此占空比(×10)
//   instant       : true 无渐变（CLI 运行时改推荐 true）
void pwm_ctrl_set_channel(uint8_t ch, bool on, uint8_t state,
                          uint32_t freq_override, uint16_t duty_override, bool instant);

// PWM 操作互斥锁：CLI 改参数/控制时全程持有，串行化所有 PWM 切换，消除与 input/temp 任务的竞态
void pwm_ctrl_lock(void);
void pwm_ctrl_unlock(void);

#endif
