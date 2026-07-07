#include "nvs_param.h"
#include "nvs_flash.h"
#include "esp_log.h"
#include <string.h>

static const char *TAG = "NVS";
#define NAMESPACE "user_cfg"

uint8_t  cfg_ok    = 0;
uint8_t  in_pin    = 10;
uint8_t  in_inv    = 0;
uint8_t  pwm_inv   = 0;
uint32_t pwm_freq[PWM_CH_CNT][2] = {{1000, 1000}, {1000, 1000}, {1000, 1000}};
uint16_t pwm_duty[PWM_CH_CNT][2] = {{500, 500}, {500, 500}, {500, 500}};
uint16_t t_rise_ms = 300;
uint16_t t_fall_ms = 300;

// NVS key：3 路 × 2 状态 的频率与占空比
static const char *k_freq[3][2] = {{"f0l", "f0h"}, {"f1l", "f1h"}, {"f2l", "f2h"}};
static const char *k_duty[3][2] = {{"d0l", "d0h"}, {"d1l", "d1h"}, {"d2l", "d2h"}};

void nvs_load_defaults(void)
{
    cfg_ok  = 0;
    in_pin  = 10;
    in_inv  = 0;
    pwm_inv = 0;
    for (int c = 0; c < PWM_CH_CNT; c++) {
        for (int s = 0; s < 2; s++) {
            pwm_freq[c][s] = 1000;   // 1 kHz
            pwm_duty[c][s] = 500;    // 50.0%
        }
    }
    t_rise_ms = 300;
    t_fall_ms = 300;
}

void nvs_read_all_param(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGW(TAG, "NVS open failed, using defaults");
        nvs_load_defaults();
        return;
    }

    nvs_load_defaults();   // 先填默认值，再用 NVS 内容覆盖（缺失项保持默认）

    nvs_get_u8(h, "cfg_ok",  &cfg_ok);
    nvs_get_u8(h, "in_pin",  &in_pin);
    nvs_get_u8(h, "in_inv",  &in_inv);
    nvs_get_u8(h, "pwm_inv", &pwm_inv);

    for (int c = 0; c < PWM_CH_CNT; c++) {
        for (int s = 0; s < 2; s++) {
            nvs_get_u32(h, k_freq[c][s], &pwm_freq[c][s]);
            nvs_get_u16(h, k_duty[c][s], &pwm_duty[c][s]);
        }
    }
    nvs_get_u16(h, "t_rise", &t_rise_ms);
    nvs_get_u16(h, "t_fall", &t_fall_ms);

    nvs_close(h);

    // 合法性钳制
    if (in_pin != 9 && in_pin != 10) in_pin = 10;

    ESP_LOGI(TAG, "loaded: cfg_ok=%d in_pin=%d in_inv=%d pwm_inv=%d trise=%d tfall=%d",
             cfg_ok, in_pin, in_inv, pwm_inv, t_rise_ms, t_fall_ms);
}

void nvs_save_all_param(void)
{
    nvs_handle_t h;
    if (nvs_open(NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
        ESP_LOGE(TAG, "NVS open(RW) failed");
        return;
    }

    nvs_set_u8(h, "cfg_ok",  cfg_ok);
    nvs_set_u8(h, "in_pin",  in_pin);
    nvs_set_u8(h, "in_inv",  in_inv);
    nvs_set_u8(h, "pwm_inv", pwm_inv);

    for (int c = 0; c < PWM_CH_CNT; c++) {
        for (int s = 0; s < 2; s++) {
            nvs_set_u32(h, k_freq[c][s], pwm_freq[c][s]);
            nvs_set_u16(h, k_duty[c][s], pwm_duty[c][s]);
        }
    }
    nvs_set_u16(h, "t_rise", t_rise_ms);
    nvs_set_u16(h, "t_fall", t_fall_ms);

    nvs_commit(h);
    nvs_close(h);
    ESP_LOGI(TAG, "saved");
}
