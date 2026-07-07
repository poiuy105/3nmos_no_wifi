#ifndef NVS_PARAM_H
#define NVS_PARAM_H

#include <stdint.h>

#define PWM_CH_CNT   3          // 3 路 PWM
#define PWM_STATE_LO 0          // 输入信号低电平对应状态
#define PWM_STATE_HI 1          // 输入信号高电平对应状态

extern uint8_t  cfg_ok;                     // 1=已配置过
extern uint8_t  in_pin;                     // 输入信号引脚：9 或 10
extern uint8_t  in_inv;                     // 输入电平逻辑取反
extern uint8_t  pwm_inv;                    // PWM 输出极性取反（3 路共享）
extern uint32_t pwm_freq[PWM_CH_CNT][2];    // [ch][lo/hi] 频率 Hz
extern uint16_t pwm_duty[PWM_CH_CNT][2];    // [ch][lo/hi] 占空比 ×10（500=50.0%）
extern uint16_t t_rise_ms;                  // 低→高 平滑切换时间 ms
extern uint16_t t_fall_ms;                  // 高→低 平滑切换时间 ms
extern int16_t  temp_thresh;                // 过温阈值 °C（默认 100）

void nvs_read_all_param(void);
void nvs_save_all_param(void);
void nvs_load_defaults(void);   // 写入出厂默认值（替代旧的 nvs_erase_factory_all）

#endif
