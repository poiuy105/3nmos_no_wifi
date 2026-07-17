#include "cli.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <errno.h>
#include "nvs_param.h"
#include "pwm_ctrl.h"
#include "input_sig.h"
#include "temp_monitor.h"
#include "key.h"
#include "board.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "driver/uart.h"
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
#include "driver/usb_serial_jtag.h"      // S3 原生 USB console 后端
#endif
#include "esp_log.h"
#include "esp_system.h"
#include "esp_chip_info.h"
#include "esp_timer.h"
#include "esp_app_desc.h"
#include "nvs_flash.h"

static const char *TAG = "CLI";

static volatile bool s_cli_debug = false;   // 事件日志开关，默认 OFF（无事件安静）
static volatile bool s_work_mode = true;

#define CLI_LINE_MAX 256
#define CLI_ARGV_MAX 8

static void cli_task(void *arg);   // 前向声明（cli_start 引用）

void cli_start(bool work_mode)
{
    s_work_mode = work_mode;
    s_cli_debug = false;
    xTaskCreate(cli_task, "cli", 4096, NULL, 3, NULL);
}

bool cli_in_work_mode(void) { return s_work_mode; }
bool cli_debug_on(void)     { return s_cli_debug; }

// ---------------- helpers ----------------
static bool parse_u32(const char *s, uint32_t *out)
{
    if (!s || !*s) return false;
    char *end; errno = 0;
    unsigned long v = strtoul(s, &end, 10);
    if (errno != 0 || *end != 0) return false;
    *out = (uint32_t)v;
    return true;
}

// 解析通道：0..2；-1=全部(all/省略，仅 allow_all)；-2=错误
static int parse_chan(const char *s, bool allow_all)
{
    if (!s || !*s) return allow_all ? -1 : -2;
    if (!strcmp(s, "all")) return allow_all ? -1 : -2;
    char *end; errno = 0;
    long v = strtol(s, &end, 10);
    if (errno != 0 || *end != 0 || v < 1 || v > PWM_CH_CNT) return -2;
    return (int)(v - 1);
}

static int parse_state(const char *s)
{
    if (!s) return -1;
    if (!strcmp(s, "lo") || !strcmp(s, "low"))  return PWM_STATE_LO;
    if (!strcmp(s, "hi") || !strcmp(s, "high")) return PWM_STATE_HI;
    return -1;
}

#define CHK_OVERTEMP(cmd) \
    do { if (temp_monitor_is_overtemp()) { printf("ERR %s: over-temp active, PWM locked\n", cmd); return -1; } } while (0)

// ---------------- pwm 运行时（不写 NVS） ----------------
static int cmd_on(int argc, char **argv)
{
    CHK_OVERTEMP("on");
    int ch = parse_chan(argc > 1 ? argv[1] : NULL, true);
    if (ch == -2) { printf("ERR on: bad channel '%s'\n", argc > 1 ? argv[1] : ""); return -1; }
    bool hi = input_sig_read_logical();
    if (ch == -1) {
        pwm_ctrl_apply_state(hi, true);
    } else {
        pwm_ctrl_set_channel((uint8_t)ch, true, hi ? PWM_STATE_HI : PWM_STATE_LO, 0, 0xFFFF, true);
    }
    printf("OK on %s\n", ch == -1 ? "all" : argv[1]);
    return 0;
}

static int cmd_off(int argc, char **argv)
{
    CHK_OVERTEMP("off");
    int ch = parse_chan(argc > 1 ? argv[1] : NULL, true);
    if (ch == -2) { printf("ERR off: bad channel\n"); return -1; }
    if (ch == -1) {
        for (int c = 0; c < PWM_CH_CNT; c++)
            pwm_ctrl_set_channel((uint8_t)c, false, 0, 0, 0xFFFF, true);
    } else {
        pwm_ctrl_set_channel((uint8_t)ch, false, 0, 0, 0xFFFF, true);
    }
    printf("OK off %s (logic 0%%%s)\n", ch == -1 ? "all" : argv[1], pwm_inv ? ", inverted" : "");
    return 0;
}

static int cmd_high(int argc, char **argv)
{
    CHK_OVERTEMP("high");
    int ch = parse_chan(argc > 1 ? argv[1] : NULL, true);
    if (ch == -2) { printf("ERR high: bad channel\n"); return -1; }
    if (ch == -1) pwm_ctrl_apply_state(true, false);    // 全部带渐变切高态
    else          pwm_ctrl_set_channel((uint8_t)ch, true, PWM_STATE_HI, 0, 0xFFFF, true);
    printf("OK high %s\n", ch == -1 ? "all" : argv[1]);
    return 0;
}

static int cmd_low(int argc, char **argv)
{
    CHK_OVERTEMP("low");
    int ch = parse_chan(argc > 1 ? argv[1] : NULL, true);
    if (ch == -2) { printf("ERR low: bad channel\n"); return -1; }
    if (ch == -1) pwm_ctrl_apply_state(false, false);
    else          pwm_ctrl_set_channel((uint8_t)ch, true, PWM_STATE_LO, 0, 0xFFFF, true);
    printf("OK low %s\n", ch == -1 ? "all" : argv[1]);
    return 0;
}

static int cmd_freq(int argc, char **argv)
{
    if (argc < 4) { printf("ERR freq: usage: freq <ch> <lo|hi> <hz>\n"); return -1; }
    CHK_OVERTEMP("freq");
    int ch = parse_chan(argv[1], false);
    if (ch < 0) { printf("ERR freq: bad channel '%s'\n", argv[1]); return -1; }
    int st = parse_state(argv[2]);
    if (st < 0) { printf("ERR freq: state must be lo|hi\n"); return -1; }
    uint32_t hz;
    if (!parse_u32(argv[3], &hz) || hz == 0) { printf("ERR freq: bad hz '%s'\n", argv[3]); return -1; }
    pwm_ctrl_lock();
    pwm_freq[ch][st] = hz;
    pwm_ctrl_set_channel((uint8_t)ch, true, (uint8_t)st, 0, 0xFFFF, true);
    pwm_ctrl_unlock();
    printf("OK freq ch%d %s = %lu Hz (temporary, not saved)\n", ch + 1, st ? "hi" : "lo", (unsigned long)hz);
    return 0;
}

static int cmd_duty(int argc, char **argv)
{
    if (argc < 4) { printf("ERR duty: usage: duty <ch> <lo|hi> <0-1000>\n"); return -1; }
    CHK_OVERTEMP("duty");
    int ch = parse_chan(argv[1], false);
    if (ch < 0) { printf("ERR duty: bad channel\n"); return -1; }
    int st = parse_state(argv[2]);
    if (st < 0) { printf("ERR duty: state must be lo|hi\n"); return -1; }
    uint32_t d;
    if (!parse_u32(argv[3], &d) || d > 1000) { printf("ERR duty: must be 0..1000\n"); return -1; }
    pwm_ctrl_lock();
    pwm_duty[ch][st] = (uint16_t)d;
    pwm_ctrl_set_channel((uint8_t)ch, true, (uint8_t)st, 0, 0xFFFF, true);
    pwm_ctrl_unlock();
    printf("OK duty ch%d %s = %u.%u%% (temporary)\n", ch + 1, st ? "hi" : "lo", (unsigned)(d / 10), (unsigned)(d % 10));
    return 0;
}

// ---------------- set 持久（写 NVS） ----------------
static int cmd_set_freq(int argc, char **argv)
{
    if (argc < 4) { printf("ERR set-freq: usage: set-freq <ch> <lo|hi> <hz>\n"); return -1; }
    int ch = parse_chan(argv[1], false);
    if (ch < 0) { printf("ERR set-freq: bad channel\n"); return -1; }
    int st = parse_state(argv[2]);
    if (st < 0) { printf("ERR set-freq: state must be lo|hi\n"); return -1; }
    uint32_t hz;
    if (!parse_u32(argv[3], &hz) || hz == 0) { printf("ERR set-freq: bad hz\n"); return -1; }
    pwm_ctrl_lock();
    pwm_freq[ch][st] = hz;
    nvs_save_all_param();
    if (!temp_monitor_is_overtemp()) pwm_ctrl_apply_state(input_sig_read_logical(), true);
    pwm_ctrl_unlock();
    printf("OK set-freq ch%d %s = %lu Hz (saved)\n", ch + 1, st ? "hi" : "lo", (unsigned long)hz);
    return 0;
}

static int cmd_set_duty(int argc, char **argv)
{
    if (argc < 4) { printf("ERR set-duty: usage: set-duty <ch> <lo|hi> <0-1000>\n"); return -1; }
    int ch = parse_chan(argv[1], false);
    if (ch < 0) { printf("ERR set-duty: bad channel\n"); return -1; }
    int st = parse_state(argv[2]);
    if (st < 0) { printf("ERR set-duty: state must be lo|hi\n"); return -1; }
    uint32_t d;
    if (!parse_u32(argv[3], &d) || d > 1000) { printf("ERR set-duty: must be 0..1000\n"); return -1; }
    pwm_ctrl_lock();
    pwm_duty[ch][st] = (uint16_t)d;
    nvs_save_all_param();
    if (!temp_monitor_is_overtemp()) pwm_ctrl_apply_state(input_sig_read_logical(), true);
    pwm_ctrl_unlock();
    printf("OK set-duty ch%d %s = %u.%u%% (saved)\n", ch + 1, st ? "hi" : "lo", (unsigned)(d / 10), (unsigned)(d % 10));
    return 0;
}

static int cmd_set_rise(int argc, char **argv)
{
    if (argc < 2) { printf("ERR set-rise: usage: set-rise <ms>\n"); return -1; }
    uint32_t ms;
    if (!parse_u32(argv[1], &ms) || ms > 60000) { printf("ERR set-rise: must be 0..60000 ms\n"); return -1; }
    t_rise_ms = (uint16_t)ms;
    nvs_save_all_param();
    printf("OK set-rise = %u ms (saved)\n", t_rise_ms);
    return 0;
}

static int cmd_set_fall(int argc, char **argv)
{
    if (argc < 2) { printf("ERR set-fall: usage: set-fall <ms>\n"); return -1; }
    uint32_t ms;
    if (!parse_u32(argv[1], &ms) || ms > 60000) { printf("ERR set-fall: must be 0..60000 ms\n"); return -1; }
    t_fall_ms = (uint16_t)ms;
    nvs_save_all_param();
    printf("OK set-fall = %u ms (saved)\n", t_fall_ms);
    return 0;
}

static int cmd_set_pin(int argc, char **argv)
{
    if (argc < 2) { printf("ERR set-pin: usage: set-pin <%d|%d>\n", PIN_IN1, PIN_IN2); return -1; }
    uint32_t p;
    if (!parse_u32(argv[1], &p) || (p != PIN_IN1 && p != PIN_IN2)) {
        printf("ERR set-pin: must be %d or %d\n", PIN_IN1, PIN_IN2); return -1;
    }
    in_pin = (uint8_t)p;
    nvs_save_all_param();
    printf("OK set-pin = GPIO%u (saved). Reboot required to take effect.\n", in_pin);
    return 0;
}

static int cmd_set_invin(int argc, char **argv)
{
    if (argc < 2) { printf("ERR set-invin: usage: set-invin <0|1>\n"); return -1; }
    uint32_t v;
    if (!parse_u32(argv[1], &v) || v > 1) { printf("ERR set-invin: must be 0 or 1\n"); return -1; }
    in_inv = (uint8_t)v;
    nvs_save_all_param();
    printf("OK set-invin = %u (saved). Reboot required to take effect.\n", in_inv);
    return 0;
}

static int cmd_set_pwminv(int argc, char **argv)
{
    if (argc < 2) { printf("ERR set-pwminv: usage: set-pwminv <0|1>\n"); return -1; }
    uint32_t v;
    if (!parse_u32(argv[1], &v) || v > 1) { printf("ERR set-pwminv: must be 0 or 1\n"); return -1; }
    pwm_inv = (uint8_t)v;
    nvs_save_all_param();
    if (!temp_monitor_is_overtemp()) pwm_ctrl_apply_state(input_sig_read_logical(), true);
    printf("OK set-pwminv = %u (saved, applied)\n", pwm_inv);
    return 0;
}

static int cmd_set_temp(int argc, char **argv)
{
    if (argc < 2) { printf("ERR set-temp: usage: set-temp <0..125>\n"); return -1; }
    uint32_t t;
    if (!parse_u32(argv[1], &t) || t > 125) { printf("ERR set-temp: must be 0..125\n"); return -1; }
    temp_thresh = (int16_t)t;
    nvs_save_all_param();
    printf("OK set-temp = %d C (saved)\n", (int)temp_thresh);
    return 0;
}

// ---------------- system（dispatch 已校验 confirm） ----------------
static int cmd_restart(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("rebooting...\n"); fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
    return 0;
}

static int cmd_config_mode(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("entering config mode...\n"); fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    key_enter_config_mode();   // 不返回
    return 0;
}

static int cmd_factory_reset(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("factory reset...\n"); fflush(stdout);
    nvs_load_defaults();
    nvs_save_all_param();
    vTaskDelay(pdMS_TO_TICKS(50));
    esp_restart();
    return 0;
}

static int cmd_erase_nvs(int argc, char **argv)
{
    (void)argc; (void)argv;
    printf("erasing NVS...\n"); fflush(stdout);
    vTaskDelay(pdMS_TO_TICKS(50));
    nvs_flash_erase();
    esp_restart();
    return 0;
}

static int cmd_debug(int argc, char **argv)
{
    if (argc < 2) { printf("debug: %s (usage: debug <on|off>)\n", s_cli_debug ? "ON" : "OFF"); return 0; }
    if (!strcmp(argv[1], "on") || !strcmp(argv[1], "1"))         s_cli_debug = true;
    else if (!strcmp(argv[1], "off") || !strcmp(argv[1], "0"))   s_cli_debug = false;
    else { printf("ERR debug: must be on|off\n"); return -1; }
    printf("OK debug %s\n", s_cli_debug ? "ON" : "OFF");
    return 0;
}

// ---------------- status 总览 ----------------
static int cmd_status(int argc, char **argv)
{
    (void)argc; (void)argv;
    const esp_app_desc_t *d = esp_app_get_description();
    esp_chip_info_t chip; esp_chip_info(&chip);
    uint32_t up = (uint32_t)(esp_timer_get_time() / 1000000);
    bool hi = input_sig_read_logical();
    int cur = hi ? PWM_STATE_HI : PWM_STATE_LO;
    bool ot = temp_monitor_is_overtemp();
    float t = temp_monitor_get_temp();

    printf("\n================ 3NMOS STATUS ================\n");
    printf("chip        : %s rev.%d\n", CONFIG_IDF_TARGET, chip.revision);
    printf("app version : %s  (build %s %s)\n", d->version, d->date, d->time);
    printf("run mode    : %s\n", s_work_mode ? "WORK" : "CONFIG");
    printf("uptime      : %02u:%02u:%02u\n", (unsigned)(up / 3600), (unsigned)((up / 60) % 60), (unsigned)(up % 60));
    printf("cfg_ok      : %u\n", cfg_ok);
    printf("input       : GPIO%u  inv=%u  level=%s  (active state=%s)\n",
           in_pin, in_inv, hi ? "HIGH" : "LOW", hi ? "HI" : "LO");
    printf("pwm_inv     : %u\n", pwm_inv);
    printf("t_rise/fall : %u ms / %u ms\n", t_rise_ms, t_fall_ms);
    printf("--- PWM (state=%s%s) ---\n", hi ? "HI" : "LO", ot ? ", OVER-TEMP LOCKED 5Hz/50%" : "");
    for (int c = 0; c < PWM_CH_CNT; c++) {
        uint32_t f = ot ? 5 : pwm_freq[c][cur];
        uint16_t du = ot ? 500 : pwm_duty[c][cur];
        printf("  ch%d  freq=%lu Hz   duty=%u.%u%%\n", c + 1,
               (unsigned long)f, (unsigned)(du / 10), (unsigned)(du % 10));
    }
    printf("--- thermal ---\n");
    if (t <= TEMP_FAULT_VALUE + 0.5f) printf("  temp       : SENSOR FAULT (not running)\n");
    else                              printf("  temp       : %.1f C\n", t);
    printf("  thresh     : %d C\n", (int)temp_thresh);
    printf("  over-temp  : %s\n", ot ? "YES" : "NO");
    printf("--- system ---\n");
    printf("  free heap  : %u B\n", (unsigned)esp_get_free_heap_size());
    printf("  min heap   : %u B\n", (unsigned)esp_get_minimum_free_heap_size());
    printf("  cli stack hw: %u B  (task prio 3)\n", (unsigned)uxTaskGetStackHighWaterMark(NULL));
    printf("  cli debug  : %s\n", s_cli_debug ? "ON" : "OFF");
    printf("==============================================\n");
    return 0;
}

// ---------------- 命令表 ----------------
static int cmd_help(int argc, char **argv);   // 前向声明（表内引用）

typedef int (*cli_cmd_fn)(int argc, char **argv);
typedef struct {
    const char *name;
    const char *group;
    const char *short_help;
    const char *long_help;
    cli_cmd_fn  handler;
    bool dangerous;
    bool work_only;
} cli_cmd_t;

static const cli_cmd_t s_cmds[] = {
    { "help", "help", "列出命令或查看某命令详情",
      "help [cmd]\n  无参=按组列出全部；带参=该命令详情", cmd_help, false, false },
    { "status", "status", "系统/PWM/温度状态总览",
      "status\n  打印全部运行态信息", cmd_status, false, false },

    { "on", "pwm", "开启某路/全部 PWM（当前态）",
      "on [ch]\n  ch=1..3 或 all，省略=全部。仅 WORK 模式", cmd_on, false, true },
    { "off", "pwm", "关闭某路/全部（逻辑 0%）",
      "off [ch]\n  逻辑占空比 0%%，受 pwm_inv 映射。仅 WORK 模式", cmd_off, false, true },
    { "high", "pwm", "切到高电平态参数",
      "high [ch]\n  省略=全部带渐变。仅 WORK 模式", cmd_high, false, true },
    { "low", "pwm", "切到低电平态参数",
      "low [ch]\n  省略=全部带渐变。仅 WORK 模式", cmd_low, false, true },
    { "freq", "pwm", "临时改频率(Hz)，不写 NVS",
      "freq <ch> <lo|hi> <hz>\n  重启丢失。仅 WORK 模式", cmd_freq, false, true },
    { "duty", "pwm", "临时改占空比(0-1000)",
      "duty <ch> <lo|hi> <0-1000>\n  500=50.0%%，重启丢失。仅 WORK 模式", cmd_duty, false, true },

    { "set-freq", "set", "持久设定频率(Hz)",
      "set-freq <ch> <lo|hi> <hz>\n  写 NVS 立即生效。仅 WORK 模式", cmd_set_freq, false, true },
    { "set-duty", "set", "持久设定占空比",
      "set-duty <ch> <lo|hi> <0-1000>\n  写 NVS 立即生效。仅 WORK 模式", cmd_set_duty, false, true },
    { "set-rise", "set", "持久设定 t_rise(ms)",
      "set-rise <ms>\n  0..60000。仅 WORK 模式", cmd_set_rise, false, true },
    { "set-fall", "set", "持久设定 t_fall(ms)",
      "set-fall <ms>\n  0..60000。仅 WORK 模式", cmd_set_fall, false, true },
    { "set-pin", "set", "持久设定输入引脚(需重启)",
      "set-pin <PIN_IN>\n  C3=6|7  S3=9|10。需重启。仅 WORK 模式", cmd_set_pin, false, true },
    { "set-invin", "set", "持久设定输入反相(需重启)",
      "set-invin <0|1>\n  需重启。仅 WORK 模式", cmd_set_invin, false, true },
    { "set-pwminv", "set", "持久设定 PWM 反相(立即生效)",
      "set-pwminv <0|1>\n  立即生效。仅 WORK 模式", cmd_set_pwminv, false, true },
    { "set-temp", "set", "持久设定过温阈值(°C)",
      "set-temp <0..125>\n  仅 WORK 模式", cmd_set_temp, false, true },

    { "restart", "system", "软重启",
      "restart confirm\n  危险：需 confirm 令牌", cmd_restart, true, false },
    { "config-mode", "system", "重启进 WiFi 配置模式",
      "config-mode confirm\n  危险：需 confirm 令牌", cmd_config_mode, true, false },
    { "factory-reset", "system", "恢复出厂默认并重启",
      "factory-reset confirm\n  危险：重载默认参数覆盖 NVS", cmd_factory_reset, true, false },
    { "erase-nvs", "system", "擦除 NVS 并重启",
      "erase-nvs confirm\n  危险：清空全部 NVS", cmd_erase_nvs, true, false },

    { "debug", "debug", "事件详细日志开关",
      "debug <on|off>\n  开启后打印 PWM 切换/输入跳变/温度采样等事件", cmd_debug, false, false },
};
#define CMD_COUNT (sizeof(s_cmds) / sizeof(s_cmds[0]))

static const cli_cmd_t *find_cmd(const char *name)
{
    for (size_t i = 0; i < CMD_COUNT; i++)
        if (!strcmp(s_cmds[i].name, name)) return &s_cmds[i];
    return NULL;
}

static int cmd_help(int argc, char **argv)
{
    if (argc >= 2) {
        const cli_cmd_t *c = find_cmd(argv[1]);
        if (!c) { printf("ERR help: no such command '%s'\n", argv[1]); return -1; }
        printf("%s\n  %s\n", c->long_help, c->short_help);
        if (c->dangerous)                 printf("  [DANGEROUS: append 'confirm' to run]\n");
        if (c->work_only && !s_work_mode) printf("  [WORK mode only]\n");
        return 0;
    }
    const char *groups[] = { "help", "status", "pwm", "set", "system", "debug" };
    printf("commands (* = dangerous, append 'confirm'; pwm/set = WORK only):\n");
    for (size_t g = 0; g < sizeof(groups) / sizeof(groups[0]); g++) {
        printf("[%s]\n", groups[g]);
        for (size_t i = 0; i < CMD_COUNT; i++) {
            if (!strcmp(s_cmds[i].group, groups[g]))
                printf("  %-12s %s%s\n", s_cmds[i].name, s_cmds[i].short_help, s_cmds[i].dangerous ? " *" : "");
        }
    }
    return 0;
}

// ---------------- console 后端抽象 ----------------
// S3 原生 USB（复位源 USB_UART_CHIP_RESET）的 console 走 USB-Serial-JTAG，而非 UART0；
// 这里按 menuconfig 选定的 console 后端选择 RX 通道。TX 始终走 ROM console（ESP_LOG/printf 不受影响）。
#if CONFIG_ESP_CONSOLE_USB_SERIAL_JTAG
static void cli_console_init(void)
{
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    usb_serial_jtag_driver_install(&cfg);   // 装 RX driver；TX/RX FIFO 全双工，与 ROM printf 不冲突
}
static inline int cli_console_read_byte(uint8_t *c)
{
    return usb_serial_jtag_read_bytes(c, 1, portMAX_DELAY);
}
#else
static void cli_console_init(void)
{
    uart_driver_install(UART_NUM_0, 1024, 0, 0, NULL, 0);   // 仅装 RX；TX 走 ROM console
}
static inline int cli_console_read_byte(uint8_t *c)
{
    return uart_read_bytes(UART_NUM_0, c, 1, portMAX_DELAY);
}
#endif

// ---------------- 行接收与分发 ----------------
static int cli_readline(char *buf, int maxlen)
{
    int i = 0;
    while (i < maxlen - 1) {
        uint8_t c;
        int r = cli_console_read_byte(&c);
        if (r <= 0) continue;
        if (c == '\r' || c == '\n') { buf[i] = 0; return i; }
        if (c == 0x08 || c == 0x7f) { if (i > 0) i--; continue; }   // 忽略退格（无行编辑）
        if (c < 0x20 || c > 0x7e) continue;                          // 非打印忽略
        buf[i++] = (char)c;
    }
    buf[maxlen - 1] = 0;
    return maxlen - 1;   // 超长截断
}

static void cli_dispatch(char *line)
{
    if (line[0] == 0 || line[0] == '#') return;   // 空行/注释（脚本友好）

    char *argv[CLI_ARGV_MAX];
    int argc = 0;
    char *save;
    for (char *tok = strtok_r(line, " \t", &save); tok && argc < CLI_ARGV_MAX;
         tok = strtok_r(NULL, " \t", &save)) {
        argv[argc++] = tok;
    }
    if (argc == 0) return;

    const cli_cmd_t *cmd = find_cmd(argv[0]);
    if (!cmd) { printf("ERR unknown command: '%s' (try 'help')\n", argv[0]); return; }
    if (cmd->work_only && !s_work_mode) {
        printf("ERR '%s' only available in WORK mode\n", argv[0]); return;
    }
    if (cmd->dangerous) {
        bool confirmed = (argc >= 2 && !strcmp(argv[1], "confirm"));
        if (!confirmed) {
            printf("WARN '%s' is destructive. Re-run as: %s confirm\n", argv[0], argv[0]);
            return;
        }
    }
    cmd->handler(argc, argv);
}

static void cli_task(void *arg)
{
    (void)arg;
    // 装 console RX driver（S3=USB-Serial-JTAG, C3=UART0）；TX 仍走 ROM console，ESP_LOG/printf 不受影响。
    cli_console_init();

    const esp_app_desc_t *d = esp_app_get_description();
    printf("\n\n==============================================\n");
    printf(" 3NMOS PWM Controller - CLI (%s)\n", CONFIG_IDF_TARGET);
    printf(" version %s   build %s %s\n", d->version, d->date, d->time);
    printf(" mode: %s   type 'help' for commands\n", s_work_mode ? "WORK" : "CONFIG");
    printf("==============================================\n");

    char line[CLI_LINE_MAX];
    while (1) {
        cli_readline(line, CLI_LINE_MAX);
        cli_dispatch(line);
    }
}
