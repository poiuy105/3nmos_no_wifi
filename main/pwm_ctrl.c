#include "pwm_ctrl.h"
#include "nvs_param.h"
#include "board.h"
#include "driver/ledc.h"
#include "soc/soc_caps.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_log.h"
#include "cli.h"

static const char *TAG = "PWM";

#define LEDC_SPEED    LEDC_LOW_SPEED_MODE
#define LEDC_CLK_SRC  LEDC_APB_CLK          // 80 MHz
#define FADE_STEPS    30                    // 软件渐变步数

// 每路独立 timer（独立频率）+ 独立 channel
static const ledc_timer_t   s_timer[PWM_CH_CNT] = {LEDC_TIMER_0, LEDC_TIMER_1, LEDC_TIMER_2};
static const ledc_channel_t s_chan[PWM_CH_CNT]  = {LEDC_CHANNEL_0, LEDC_CHANNEL_1, LEDC_CHANNEL_2};
static const int            s_gpio[PWM_CH_CNT]  = {PIN_PWM1, PIN_PWM2, PIN_PWM3};

static uint32_t s_cur_freq[PWM_CH_CNT];
static int      s_cur_res[PWM_CH_CNT];

// PWM 操作互斥锁（递归 + 优先级继承）：保护 apply_state/set_channel/overtemp_alert，
// 串行化所有 PWM 切换，消除与 input_sig/temp_monitor 任务的 LEDC 寄存器竞态。
static StaticSemaphore_t s_mtx_buf;
static SemaphoreHandle_t s_pwm_mtx;

void pwm_ctrl_lock(void)   { if (s_pwm_mtx) xSemaphoreTakeRecursive(s_pwm_mtx, portMAX_DELAY); }
void pwm_ctrl_unlock(void) { if (s_pwm_mtx) xSemaphoreGiveRecursive(s_pwm_mtx); }

// 动态选择 duty_resolution：从芯片最大位宽向下找满足 2<=div<=1024 的最大 res
static int pick_resolution(uint32_t freq)
{
    if (freq == 0) freq = 1;
    for (int res = SOC_LEDC_TIMER_BIT_WIDTH; res >= 1; res--) {
        uint64_t denom = (uint64_t)freq << res;          // freq * 2^res
        if (denom == 0) continue;
        if (denom * 2ULL    <= 80000000ULL &&
            denom * 1024ULL >= 80000000ULL) {
            return res;
        }
    }
    return SOC_LEDC_TIMER_BIT_WIDTH;
}

static uint32_t duty_to_tick(uint16_t duty_x10, int res)
{
    uint32_t maxv = (1UL << res) - 1;
    uint32_t tick = ((uint32_t)duty_x10 * maxv) / 1000UL;
    if (pwm_inv) tick = maxv - tick;
    return tick;
}

// 频率瞬切（必要时重配 timer），并把当前 duty 按 res 缩放消除跳变；返回目标 tick
static uint32_t channel_prep(int c, uint32_t freq_B, uint16_t duty_B_x10)
{
    ledc_channel_t ch = s_chan[c];
    uint32_t cur_tick = (uint32_t)ledc_get_duty(LEDC_SPEED, ch);
    int res_A = s_cur_res[c];
    int res_B = pick_resolution(freq_B);

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
        uint32_t maxA = (1UL << res_A) - 1;
        uint32_t maxB = (1UL << res_B) - 1;
        if (maxA) cur_tick = cur_tick * maxB / maxA;
        ledc_set_duty(LEDC_SPEED, ch, cur_tick);
        ledc_update_duty(LEDC_SPEED, ch);
    }
    return duty_to_tick(duty_B_x10, res_B);
}

void pwm_ctrl_init(void)
{
    s_pwm_mtx = xSemaphoreCreateRecursiveMutexStatic(&s_mtx_buf);

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
    ESP_LOGI(TAG, "3 channels ready");
}

// 软件渐变：在调用任务上下文里逐步 set_duty，每步 vTaskDelay 让出 CPU，避免饿死 IDLE 看门狗
void pwm_ctrl_apply_state(bool logical_high, bool instant)
{
    pwm_ctrl_lock();
    int state = logical_high ? PWM_STATE_HI : PWM_STATE_LO;
    uint32_t T = instant ? 0 : (logical_high ? t_rise_ms : t_fall_ms);

    uint32_t target[PWM_CH_CNT], cur[PWM_CH_CNT];
    for (int c = 0; c < PWM_CH_CNT; c++) {
        target[c] = channel_prep(c, pwm_freq[c][state], pwm_duty[c][state]);
        cur[c]    = (uint32_t)ledc_get_duty(LEDC_SPEED, s_chan[c]);
    }

    int steps = instant ? 1 : FADE_STEPS;
    uint32_t step_ms = (!instant && T / steps > 0) ? (T / steps) : 1;
    // step_ms<10（默认 100Hz tick）时 pdMS_TO_TICKS 为 0，vTaskDelay(0) 只 yield 不阻塞，
    // 会让渐变循环近似忙等、饿死 IDLE 看门狗。这里保证至少 1 个 tick。
    TickType_t step_tick = pdMS_TO_TICKS(step_ms);
    if (!instant && step_tick == 0) step_tick = 1;

    for (int i = 1; i <= steps; i++) {
        for (int c = 0; c < PWM_CH_CNT; c++) {
            int32_t d = (int32_t)cur[c] + ((int32_t)target[c] - (int32_t)cur[c]) * i / steps;
            ledc_set_duty(LEDC_SPEED, s_chan[c], (uint32_t)d);
            ledc_update_duty(LEDC_SPEED, s_chan[c]);
        }
        if (!instant) vTaskDelay(step_tick);
    }

    CLI_DEBUG(TAG, "apply %s-state fade=%lums duty=[%lu %lu %lu]",
              logical_high ? "HI" : "LO", (unsigned long)T,
              (unsigned long)target[0], (unsigned long)target[1], (unsigned long)target[2]);
    pwm_ctrl_unlock();
}

void pwm_ctrl_overtemp_alert(bool on)
{
    pwm_ctrl_lock();
    if (on) {
        ESP_LOGW(TAG, "over-temp alert: 3ch -> 5Hz/50%%");
        // C3 LEDC 最低频率约 5Hz，用 5Hz 代替 1Hz（1Hz 超出 C3 分频范围）
        for (int c = 0; c < PWM_CH_CNT; c++) {
            channel_prep(c, 5, 500);
            ledc_set_duty(LEDC_SPEED, s_chan[c], duty_to_tick(500, s_cur_res[c]));
            ledc_update_duty(LEDC_SPEED, s_chan[c]);
        }
    }
    CLI_DEBUG(TAG, "over-temp alert %s", on ? "ON" : "OFF");
    // off: 由调用方 pwm_ctrl_apply_state() 恢复
    pwm_ctrl_unlock();
}

// 运行时设置单通道输出（不写 NVS）。单通道仅瞬切；渐变由 apply_state 整体处理。
void pwm_ctrl_set_channel(uint8_t ch, bool on, uint8_t state,
                          uint32_t freq_override, uint16_t duty_override, bool instant)
{
    (void)instant;   // 保留参数匹配接口语义；单通道仅瞬切
    if (ch >= PWM_CH_CNT) {
        return;
    }
    pwm_ctrl_lock();

    uint32_t freq = (freq_override > 0) ? freq_override : pwm_freq[ch][state];
    uint16_t duty = (duty_override != 0xFFFF) ? duty_override : pwm_duty[ch][state];
    if (!on) duty = 0;   // 逻辑 0%（经 duty_to_tick 受 pwm_inv 映射）

    uint32_t target_tick = channel_prep(ch, freq, duty);
    ledc_set_duty(LEDC_SPEED, s_chan[ch], target_tick);
    ledc_update_duty(LEDC_SPEED, s_chan[ch]);

    CLI_DEBUG(TAG, "set ch=%u on=%d state=%u freq=%lu duty=%u",
              ch, on, state, (unsigned long)freq, duty);
    pwm_ctrl_unlock();
}
