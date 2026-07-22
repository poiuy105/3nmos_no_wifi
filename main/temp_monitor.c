#include "temp_monitor.h"
#include "app.h"            // LED_PIN
#include "nvs_param.h"      // temp_thresh
#include "pwm_ctrl.h"
#include "input_sig.h"      // input_sig_read_logical (恢复输出时用)
#include "onewire_bus.h"
#include "ds18b20.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/gpio.h"
#include "esp_timer.h"
#include "esp_log.h"
#include "esp_err.h"
#include "cli.h"

static const char *TAG = "TEMP";

#define HYSTERESIS_DEG   5                // 解除滞后 °C
#define SAMPLE_PERIOD_MS 2000             // 采样周期
#define ALERT_TOGGLE_US  500000           // LED 翻转 500ms -> 1Hz/50%
#if CONFIG_IDF_TARGET_ESP32S3
#define FAN_ON_TEMP      40               // 风扇开启阈值 °C 默认值（实际用 fan_on_temp NVS 参数）
static volatile fan_mode_t s_fan_mode = FAN_MODE_AUTO;
static volatile bool       s_fan_on  = false;
#endif

static volatile bool  s_overtemp  = false;
static volatile float s_last_temp = TEMP_FAULT_VALUE;
static ds18b20_device_handle_t s_ds = NULL;
static esp_timer_handle_t s_led_timer = NULL;
static volatile bool s_led_on = false;

bool  temp_monitor_is_overtemp(void) { return s_overtemp; }
float temp_monitor_get_temp(void)    { return s_last_temp; }
#if CONFIG_IDF_TARGET_ESP32S3
void      temp_monitor_fan_set_mode(fan_mode_t mode) { s_fan_mode = mode; }
fan_mode_t temp_monitor_fan_get_mode(void)            { return s_fan_mode; }
bool      temp_monitor_fan_is_on(void)                { return s_fan_on; }
#endif

// 独立定时器翻转 LED，避免被 DS18B20 750ms 转换阻塞影响
static void led_timer_cb(void *arg)
{
    s_led_on = !s_led_on;
    gpio_set_level(LED_PIN, s_led_on ? 0 : 1);   // 低电平点亮
}

static esp_err_t init_ds18b20(void)
{
    onewire_bus_handle_t bus = NULL;
    onewire_bus_config_t bus_cfg = { .bus_gpio_num = TEMP_SENSOR_GPIO };
    onewire_bus_rmt_config_t rmt_cfg = {
        .max_rx_bytes = 10,   // 1 字节 ROM 命令 + 8 字节 ROM 号 + 1 字节设备命令
    };
    esp_err_t err = onewire_new_bus_rmt(&bus_cfg, &rmt_cfg, &bus);
    if (err != ESP_OK) { ESP_LOGE(TAG, "1-wire bus fail: %s", esp_err_to_name(err)); return err; }
    gpio_set_pull_mode(TEMP_SENSOR_GPIO, GPIO_PULLUP_ONLY);   // 启用内部上拉（无外部上拉时必需）

    onewire_device_iter_handle_t iter = NULL;
    onewire_new_device_iter(bus, &iter);
    onewire_device_t dev;
    err = onewire_device_iter_get_next(iter, &dev);
    if (err != ESP_OK) { ESP_LOGE(TAG, "no 1-wire device found"); return err; }
    ESP_LOGI(TAG, "1-wire device address=0x%016llX", (unsigned long long)dev.address);

    ds18b20_config_t dcfg;
    err = ds18b20_new_device(&dev, &dcfg, &s_ds);
    if (err != ESP_OK) { ESP_LOGE(TAG, "ds18b20 init fail"); return err; }
    ds18b20_set_resolution(s_ds, DS18B20_RESOLUTION_12B);
    return ESP_OK;
}

static esp_err_t read_temp(float *out)
{
    if (!s_ds) return ESP_FAIL;
    esp_err_t err = ds18b20_trigger_temperature_conversion(s_ds);
    if (err != ESP_OK) return err;
    vTaskDelay(pdMS_TO_TICKS(750));                 // 12bit 转换时间
    return ds18b20_get_temperature(s_ds, out);
}

static void temp_task(void *arg)
{
    bool sensor_ok = (init_ds18b20() == ESP_OK);
    if (!sensor_ok) ESP_LOGW(TAG, "DS18B20 unavailable, over-temp protection disabled");

    bool alert = false;

    while (1) {
        if (sensor_ok) {
            float t;
            if (read_temp(&t) == ESP_OK) {
                s_last_temp = t;
                CLI_DEBUG(TAG, "temp=%.1fC thr=%d alert=%d", t, temp_thresh, alert);
                // 带滞后的状态机
                if (!alert && t > (float)temp_thresh) {
                    alert = true;
                    ESP_LOGW(TAG, "OVER-TEMP ON (t=%.1f > %d)", t, temp_thresh);
                } else if (alert && t < (float)(temp_thresh - HYSTERESIS_DEG)) {
                    alert = false;
                    ESP_LOGW(TAG, "OVER-TEMP OFF (t=%.1f < %d)", t, temp_thresh - HYSTERESIS_DEG);
                }
            } else {
                ESP_LOGW(TAG, "read failed, keep state (alert=%d)", alert);   // 故障忽略
            }
        }

        // 进入/退出提示
        if (alert && !s_overtemp) {
            s_overtemp = true;
            s_led_on = true;
            gpio_set_level(LED_PIN, 0);                 // 立即点亮
            esp_timer_start_periodic(s_led_timer, ALERT_TOGGLE_US);
            pwm_ctrl_overtemp_alert(true);
        } else if (!alert && s_overtemp) {
            s_overtemp = false;
            esp_timer_stop(s_led_timer);
            gpio_set_level(LED_PIN, 1);                 // 熄灭（恢复工作态）
            pwm_ctrl_overtemp_alert(false);
            pwm_ctrl_apply_state(input_sig_read_logical(), true);   // 恢复当前输入对应输出
        }

#if CONFIG_IDF_TARGET_ESP32S3
        // 风扇控制（每周期）：auto=按 fan_on_temp, on=强制开, off=强制关
        {
            bool want_on;
            if (s_fan_mode == FAN_MODE_ON)       want_on = true;
            else if (s_fan_mode == FAN_MODE_OFF) want_on = false;
            else                                 want_on = (s_last_temp > (float)fan_on_temp);
            s_fan_on = want_on;
            gpio_set_level(PIN_FAN, want_on ? 1 : 0);
        }
#endif

        vTaskDelay(pdMS_TO_TICKS(SAMPLE_PERIOD_MS));
    }
}

void temp_monitor_start(void)
{
    const esp_timer_create_args_t tcfg = {
        .callback = led_timer_cb,
        .name = "ot_led"
    };
    esp_timer_create(&tcfg, &s_led_timer);

#if CONFIG_IDF_TARGET_ESP32S3
    // 风扇引脚初始化（高电平=开，初始关闭）
    gpio_config_t fan_io = {0};
    fan_io.pin_bit_mask = (1ULL << PIN_FAN);
    fan_io.mode         = GPIO_MODE_OUTPUT;
    fan_io.pull_up_en   = GPIO_PULLUP_DISABLE;
    fan_io.pull_down_en = GPIO_PULLDOWN_DISABLE;
    gpio_config(&fan_io);
    gpio_set_level(PIN_FAN, 0);
    ESP_LOGI(TAG, "fan control on GPIO%d (on > %dC)", PIN_FAN, FAN_ON_TEMP);
#endif

    xTaskCreate(temp_task, "temp", 4096, NULL, 5, NULL);
}
