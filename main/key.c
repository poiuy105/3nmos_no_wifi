#include "key.h"
#include "app.h"
#include "temp_monitor.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_system.h"
#include "esp_sleep.h"      // RTC_NOINIT_ATTR
#include "esp_log.h"

static const char *TAG = "KEY";

#define LONG_PRESS_MS   5000
#define FAST_TOGGLE_MS  100
#define RTC_MAGIC       0xA5A55A5AU

// 软重启后保留的 RTC 内存：传递"进配置页面"意图
static RTC_NOINIT_ATTR uint32_t s_rtc_magic;
static RTC_NOINIT_ATTR uint8_t  s_rtc_enter_cfg;

bool key_consume_enter_config(void)
{
    if (s_rtc_magic == RTC_MAGIC && s_rtc_enter_cfg) {
        s_rtc_enter_cfg = 0;
        return true;
    }
    return false;
}

static void key_task(void *arg)
{
    int state = 0;                 // 0=IDLE, 1=PRESSED
    TickType_t t0 = 0, last_toggle = 0;
    bool led_on = false;

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(10));
        bool pressed = (gpio_get_level(KEY_PIN) == 0);   // 按下为低

        if (state == 0) {
            if (pressed) {
                state = 1;
                t0 = xTaskGetTickCount();
                last_toggle = t0;
                led_on = false;
            }
        } else {  // PRESSED
            if (pressed) {
                TickType_t now = xTaskGetTickCount();
                uint32_t held_ms = (uint32_t)(now - t0) * portTICK_PERIOD_MS;
                if (held_ms >= LONG_PRESS_MS) {
                    // 长按达标：LED 常亮 1s → 置标志 → 重启进配置
                    if (!temp_monitor_is_overtemp()) gpio_set_level(LED_PIN, 0);   // 点亮（过温时让 temp 任务管 LED）
                    ESP_LOGW(TAG, "long-press 5s -> restart into config");
                    s_rtc_magic = RTC_MAGIC;
                    s_rtc_enter_cfg = 1;
                    vTaskDelay(pdMS_TO_TICKS(1000));
                    esp_restart();
                }
                // 计时中：LED 快闪反馈（过温时不碰 LED，由温度任务 1Hz 闪烁）
                if (!temp_monitor_is_overtemp() &&
                    (uint32_t)(now - last_toggle) * portTICK_PERIOD_MS >= FAST_TOGGLE_MS) {
                    led_on = !led_on;
                    gpio_set_level(LED_PIN, led_on ? 0 : 1);
                    last_toggle = now;
                }
            } else {
                // 松开 = 短按：LED 亮 50ms 反馈，随后熄灭（过温时跳过，避免与温度任务抢 LED）
                if (!temp_monitor_is_overtemp()) {
                    gpio_set_level(LED_PIN, 0);
                    vTaskDelay(pdMS_TO_TICKS(50));
                    gpio_set_level(LED_PIN, 1);
                }
                state = 0;
                ESP_LOGI(TAG, "short press");
            }
        }
    }
}

void key_task_start(void)
{
    xTaskCreate(key_task, "key", 2048, NULL, 4, NULL);
}
