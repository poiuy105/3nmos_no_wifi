#include "pwm_ctrl.h"
#include "nvs_param.h"
#include "board.h"
#include "driver/ledc.h"
#include "soc/soc_caps.h"
#include "esp_log.h"
#include "esp_idf_version.h"

static const char *TAG = "PWM";

#define LEDC_SPEED    LEDC_LOW_SPEED_MODE
#define LEDC_CLK_SRC  LEDC_APB_CLK          // 80 MHz，精度最高

// 每路独立 timer（保证独立频率）+ 独立 channel
// PWM1=GPIO48, PWM2=GPIO21, PWM3=GPIO47
static const ledc_timer_t   s_timer[PWM_CH_CNT] = {LEDC_TIMER_0, LEDC_TIMER_1, LEDC_TIMER_2};
static const ledc_channel_t s_chan[PWM_CH_CNT]  = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2};
static const int            s_gpio[PWM_CH_CNT]  = {PIN_PWM1, PIN_PWM2, PIN_PWM3};

// 每通道当前生效的频率与分辨率（用于切换时判断/缩放）
static uint32_t s_cur_freq[PWM_CH_CNT];
static int      s_cur_res[PWM_CH_CNT];

// 动态选择 duty_resolution：从高到低找满足 2 <= div <= 1024 的最大 res
// 其中 div = 80MHz / (freq * 2^res)。覆盖 1Hz~40kHz 且占空比精度尽量高。
static int pick_resolution(uint32_t freq)
{
    if (freq == 0) freq = 1;
    for (int res = SOC_LEDC_TIMER_BIT_WIDTH; res >= 1; res--) {   // 受芯片位宽上限约束（C3=14, S3=20）
        uint64_t denom = (uint64_t)freq << res;          // freq * 2^res
        if (denom == 0) continue;
        // div = 80e6 / denom，要求 2 <= div <= 1024
        if (denom * 2ULL    <= 80000000ULL &&
            denom * 1024ULL >= 80000000ULL) {
            return res;
        }
    }
    return SOC_LEDC_TIMER_BIT_WIDTH;   // 兜底，不超过芯片上限
}

// 占空比 ×10（500=50.0%）→ res 下的 tick；若 pwm_inv 则取反
static uint32_t duty_to_tick(uint16_t duty_x10, int res)
{
    uint32_t maxv = (1UL << res) - 1;
    uint32_t tick = ((uint32_t)duty_x10 * maxv) / 1000UL;
    if (pwm_inv) tick = maxv - tick;
    return tick;
}

// 单通道切换到 (freq_B, duty_B)，fade 时长 T_ms
static void channel_apply(int c, uint32_t freq_B, uint16_t duty_B_x10, uint32_t T_ms)
{
    ledc_channel_t ch = s_chan[c];

    // 1. 停止进行中的 fade，读取当前实际 tick
#if ESP_IDF_VERSION >= ESP_IDF_VERSION_VAL(5, 0, 0)
    ledc_fade_stop(LEDC_SPEED, ch);
#endif
    uint32_t cur_tick = (uint32_t)ledc_get_duty(LEDC_SPEED, ch);

    int res_A = s_cur_res[c];
    int res_B = pick_resolution(freq_B);

    // 3. 频率变化：重配 timer（频率瞬间切换）
    if (freq_B != s_cur_freq[c]) {
        ledc_timer_config_t tcfg = {
            .speed_mode      = LEDC_SPEED,
            .timer_num       = s_timer[c],
            .clk_cfg         = LEDC_CLK_SRC,
            .duty_resolution = (ledc_timer_bit_t)res_B,
            .freq_hz         = freq_B,
        };
        ledc_timer_config(&tcfg);
        s_cur_freq[c] = freq_B;
        s_cur_res[c]  = res_B;
        // 当前 tick 从 res_A 缩放到 res_B，消除输出跳变
        uint32_t maxA = (1UL << res_A) - 1;
        uint32_t maxB = (1UL << res_B) - 1;
        if (maxA) cur_tick = cur_tick * maxB / maxA;
        ledc_set_duty(LEDC_SPEED, ch, cur_tick);
        ledc_update_duty(LEDC_SPEED, ch);
    } else {
        res_B = res_A;   // 频率不变则分辨率不变
    }

    // 4/5. 目标 tick：瞬切 或 硬件 fade
    uint32_t target = duty_to_tick(duty_B_x10, res_B);
    if (T_ms == 0) {
        ledc_set_duty(LEDC_SPEED, ch, target);
        ledc_update_duty(LEDC_SPEED, ch);
    } else {
        ledc_set_fade_with_time(LEDC_SPEED, ch, target, T_ms);
        ledc_fade_start(LEDC_SPEED, ch, LEDC_FADE_NO_WAIT);
    }
}

void pwm_ctrl_init(void)
{
    ledc_fade_func_install(0);   // 启用 fade 中断（全系统一次）

    // 上电按"低电平态"频率初始化各 timer/channel（之后 apply_state 会切到实际态）
    for (int c = 0; c < PWM_CH_CNT; c++) {
        uint32_t f = pwm_freq[c][PWM_STATE_LO];
        int res = pick_resolution(f);

        ledc_timer_config_t tcfg = {
            .speed_mode      = LEDC_SPEED,
            .timer_num       = s_timer[c],
            .clk_cfg         = LEDC_CLK_SRC,
            .duty_resolution = (ledc_timer_bit_t)res,
            .freq_hz         = f,
        };
        ledc_timer_config(&tcfg);

        ledc_channel_config_t ccfg = {
            .gpio_num   = s_gpio[c],
            .speed_mode = LEDC_SPEED,
            .channel    = s_chan[c],
            .intr_type  = LEDC_INTR_DISABLE,
            .timer_sel  = s_timer[c],
            .duty       = 0,
            .hpoint     = 0,
        };
        ledc_channel_config(&ccfg);

        s_cur_freq[c] = f;
        s_cur_res[c]  = res;
    }
    ESP_LOGI(TAG, "3 channels ready (GPIO48/21/47)");
}

void pwm_ctrl_apply_state(bool logical_high, bool instant)
{
    int state = logical_high ? PWM_STATE_HI : PWM_STATE_LO;
    uint32_t T = instant ? 0 : (logical_high ? t_rise_ms : t_fall_ms);

    ESP_LOGD(TAG, "apply %s-state (fade=%lu ms)", logical_high ? "HIGH" : "LOW", (unsigned long)T);

    for (int c = 0; c < PWM_CH_CNT; c++) {
        channel_apply(c, pwm_freq[c][state], pwm_duty[c][state], T);
    }
}

void pwm_ctrl_overtemp_alert(bool on)
{
    if (on) {
        ESP_LOGW(TAG, "over-temp alert: 3ch -> 1Hz/50%%");
        for (int c = 0; c < PWM_CH_CNT; c++) {
            channel_apply(c, 1, 500, 0);   // 1Hz, 50.0%, 瞬切
        }
    }
    // off: 由调用方 pwm_ctrl_apply_state() 恢复当前输入状态
}
