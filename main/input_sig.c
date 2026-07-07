#include "input_sig.h"
#include "nvs_param.h"
#include "pwm_ctrl.h"
#include "driver/gpio.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"

static const char *TAG = "INPUT";

#define POLL_MS     5   // 轮询周期
#define DEBOUNCE_MS 30  // 消抖稳定时间

static void input_sig_task(void *arg);

void input_sig_init(void)
{
    gpio_config_t io = {0};
    io.pin_bit_mask = (1ULL << in_pin);
    io.mode = GPIO_MODE_INPUT;
    io.pull_up_en   = GPIO_PULLUP_DISABLE;    // 由外部信号驱动
    io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io);
    ESP_LOGI(TAG, "init pin=GPIO%d inv=%d", in_pin, in_inv);
}

bool input_sig_read_logical(void)
{
    int raw = gpio_get_level(in_pin);          // 0 或 1
    bool logical = (raw != 0);
    return in_inv ? !logical : logical;
}

void input_sig_start(void)
{
    xTaskCreate(input_sig_task, "input_sig", 2048, NULL, 5, NULL);
}

static void input_sig_task(void *arg)
{
    bool last       = input_sig_read_logical();   // 已确认的逻辑电平
    bool candidate  = last;                       // 待确认的候选电平
    int  debounce   = 0;

    while (1) {
        bool cur = input_sig_read_logical();
        if (cur != candidate) {
            candidate = cur;
            debounce  = 0;
        } else if (candidate != last) {
            debounce += POLL_MS;
            if (debounce >= DEBOUNCE_MS) {
                last = candidate;
                ESP_LOGI(TAG, "logical level -> %s", last ? "HIGH" : "LOW");
                pwm_ctrl_apply_state(last, false);   // 平滑切换
            }
        }
        vTaskDelay(pdMS_TO_TICKS(POLL_MS));
    }
}
