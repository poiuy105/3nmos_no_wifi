#include "app.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_log.h"
#include "esp_system.h"
#include "nvs_flash.h"
#include "driver/gpio.h"
#include "nvs_param.h"
#include "pwm_ctrl.h"
#include "input_sig.h"
#include "config_portal.h"
#include "key.h"

static const char *TAG = "MAIN";

// 配置模式 LED 慢闪（1Hz）
static void led_blink_task(void *arg)
{
    bool on = false;
    while (1) {
        gpio_set_level(LED_PIN, on ? 0 : 1);
        on = !on;
        vTaskDelay(pdMS_TO_TICKS(500));
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "=== 3NMOS PWM Controller (ESP32-S3) ===");

    // NVS
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ESP_ERROR_CHECK(nvs_flash_init());
    }
    nvs_read_all_param();

    // GPIO: LED（输出，初始熄灭）
    gpio_config_t io = {0};
    io.pin_bit_mask  = (1ULL << LED_PIN);
    io.mode          = GPIO_MODE_OUTPUT;
    io.pull_up_en    = GPIO_PULLUP_DISABLE;
    io.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io);
    gpio_set_level(LED_PIN, 1);   // 熄灭（低电平点亮）

    // GPIO: KEY（输入 + 内部上拉）
    io.pin_bit_mask  = (1ULL << KEY_PIN);
    io.mode          = GPIO_MODE_INPUT;
    io.pull_up_en    = GPIO_PULLUP_ENABLE;
    io.pull_down_en  = GPIO_PULLDOWN_DISABLE;
    gpio_config(&io);

    // 判定启动模式
    bool go_config = key_consume_enter_config();   // 长按触发的 RTC 标志
    if (cfg_ok == 0) go_config = true;             // 首次未配置

    if (go_config) {
        ESP_LOGI(TAG, ">>> CONFIG MODE");
        xTaskCreate(led_blink_task, "led_cfg", 1024, NULL, 2, NULL);
        config_portal_start();   // 阻塞：保存/重置后内部 esp_restart() 不返回
    } else {
        ESP_LOGI(TAG, ">>> WORK MODE");
        input_sig_init();                       // 配置输入信号引脚
        pwm_ctrl_init();                        // 3 路 LEDC
        bool hi = input_sig_read_logical();     // 读当前输入电平
        pwm_ctrl_apply_state(hi, true);         // 上电立即输出（无 fade）
        input_sig_start();                      // 启动轮询 + 平滑切换
        key_task_start();                       // 按键常驻任务
        // 工作模式：LED 保持熄灭
    }
}
