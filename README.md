# SYNAFLOW PWM 控制器（ESP32-S3 / ESP32-C3 离线固件）

基于 ESP32-S3 或 ESP32-C3 的 3 路独立 PWM 控制器，**完全离线运行**（不联网、无 MQTT）。手机连接设备热点即可在本地配置页面设置参数；由一路数字输入信号的高低电平驱动 3 路 PWM 输出，电平切换时占空比平滑过渡。支持 DS18B20 过温保护与串口命令行（CLI）控制。

## 功能特性

- **离线运行**：上电即按本地配置工作，无需路由器或服务器。
- **本地配置页**：SoftAP + Captive Portal，手机连热点自动弹出配置页面。
- **双芯片支持**：同一套代码编译 ESP32-S3 与 ESP32-C3 两种固件。
- **3 路独立 PWM**：每路独立设置高/低电平状态的频率与占空比。
- **平滑切换**：输入电平变化时占空比软件渐变平滑过渡、频率瞬间切换；高→低、低→高 各自独立切换时间。
- **双反向开关**：输入电平逻辑取反、PWM 输出极性取反（独立可配）。
- **过温保护**：DS18B20 测温，超阈值时 3 路切 5Hz/50% 提示 + LED 1Hz 闪烁，带 5°C 滞后。
- **串口 CLI**：21 条命令，支持运行时控 PWM、持久改参数、状态总览、系统操作、事件日志。
- **按键控制**：短按 LED 反馈，长按 5 秒进入配置页面。
- **掉电保存**：所有参数存于 NVS，断电不丢失。

## 硬件

- 芯片：ESP32-S3 或 ESP32-C3（编译时选择）
- 串口控制台（CLI / 日志）：115200-8N1
  - **ESP32-S3**：原生 USB（USB-Serial-JTAG，GPIO19=D- / GPIO20=D+），USB 数据线直连电脑，免 USB-TTL 桥
  - **ESP32-C3**：UART0（TX=GPIO21 / RX=GPIO20），需 USB-TTL 桥

### 引脚定义

**ESP32-S3：**

| 引脚 | 功能 | 说明 |
|------|------|------|
| GPIO0  | 按键       | 内部上拉，按下拉低（strapping 脚，**上电时勿按住**）|
| GPIO15 | LED        | 低电平点亮 |
| GPIO10 | 输入信号   | 与 GPIO9 二选一（配置项，默认）|
| GPIO9  | 输入信号   | 与 GPIO10 二选一（配置项）|
| GPIO48 | PWM1       | LEDC，独立频率/占空比 |
| GPIO21 | PWM2       | LEDC，独立频率/占空比 |
| GPIO47 | PWM3       | LEDC，独立频率/占空比 |
| GPIO14 | DS18B20    | 单总线温度传感器 |
| GPIO19/20 | 原生 USB | D-/D+（USB-Serial-JTAG）|

**ESP32-C3：**

| 引脚 | 功能 | 说明 |
|------|------|------|
| GPIO9  | 按键       | 内部上拉，按下拉低（strapping 脚）|
| GPIO3  | LED        | 低电平点亮 |
| GPIO6  | 输入信号   | 与 GPIO7 二选一（配置项）|
| GPIO7  | 输入信号   | 与 GPIO6 二选一（配置项，默认）|
| GPIO4  | PWM1       | LEDC，独立频率/占空比 |
| GPIO5  | PWM2       | LEDC，独立频率/占空比 |
| GPIO10 | PWM3       | LEDC，独立频率/占空比 |
| GPIO2  | DS18B20    | 单总线温度传感器 |
| GPIO21/20 | UART0   | TX/RX（串口控制台）|

> 所有引脚均已避开 strapping / SPI-flash 冲突。

### LED 状态

| 状态 | LED 表现 |
|------|----------|
| 工作模式     | 熄灭 |
| 配置模式     | 慢闪 1Hz |
| 过温报警     | 1Hz 闪烁（由温度任务控制）|
| 短按按键     | 快闪 1 次 |
| 长按计时中   | 持续快闪 |
| 长按满 5 秒  | 常亮 1 秒后重启进配置 |

## 使用方法

### 1. 配置参数（首次必做）

1. 上电。首次（NVS 为空）会自动进入配置模式；之后可**长按按键 5 秒**进入，或串口发 `config-mode confirm`。
2. 手机连接热点 `SYNAFLOW-XXXXXX`（XXXXXX 为设备 MAC 后缀，无密码）。
3. 浏览器自动弹出配置页，或手动访问 `http://192.168.4.1`。
4. 配置参数（见下表），点击「保存并重启」。

| 配置项 | 范围 | 默认 |
|--------|------|------|
| 输入信号引脚        | S3:GPIO10/GPIO9；C3:GPIO6/GPIO7 | S3:GPIO10 / C3:GPIO7 |
| 输入电平反向        | 开 / 关                   | 关 |
| PWM 极性反向（3 路共享）| 开 / 关                 | 关 |
| 各路 PWM 低电平频率  | 1 ~ 40000 Hz             | 1000 Hz |
| 各路 PWM 低电平占空比| 0.0 ~ 100.0 %            | 50.0% |
| 各路 PWM 高电平频率  | 1 ~ 40000 Hz             | 1000 Hz |
| 各路 PWM 高电平占空比| 0.0 ~ 100.0 %            | 50.0% |
| 低→高 切换时间       | 0 ~ 60000 ms             | 300 ms |
| 高→低 切换时间       | 0 ~ 60000 ms             | 300 ms |
| 过温阈值             | 0 ~ 125 °C              | 100 °C |

### 2. 工作模式

保存重启后进入工作模式。设备根据输入信号电平输出对应状态的 3 路 PWM：

- 输入**低电平** → 输出"低电平态"频率/占空比
- 输入**高电平** → 输出"高电平态"频率/占空比
- 电平切换时，占空比按配置时间平滑过渡（软件渐变），频率瞬间切换

---

## 串口 CLI（命令行控制）

设备支持通过串口进行交互式控制、状态查询与调试。**WORK / CONFIG 两种模式都启用 CLI**。

### 连接方式

| 芯片 | 接口 | 波特率 | 连接方式 |
|------|------|--------|----------|
| ESP32-S3 | 原生 USB（USB-Serial-JTAG）| 115200-8N1 | USB 数据线直连电脑 |
| ESP32-C3 | UART0（TX=GPIO21, RX=GPIO20）| 115200-8N1 | USB-TTL 桥 |

**串口助手设置**：波特率 115200，数据位 8，无校验，停止位 1；**务必勾选"发送新行 / Add LF / 发送 CR+LF"**——CLI 靠回车（`\r` 或 `\n`）识别一行结束，不发回车命令不会被处理。

上电后串口先打印一段 banner：

```
==============================================
 SYNAFLOW PWM Controller - CLI (esp32s3)
 version 44f8e35   build Jul 17 2026 ...
 mode: WORK   type 'help' for commands
==============================================
```

### 命令格式与约定

- 每行一条命令，命令与参数用空格分隔，以回车结束。
- `#` 开头的行视为注释（便于脚本批处理）。
- 输出约定：成功 `OK ...`，错误 `ERR <cmd>: <原因>`（便于脚本 `grep ERR`）。
- **危险命令需追加 `confirm` 令牌**二次确认（避免误触发重启/擦除）。
- `pwm` / `set-*` 类命令仅 **WORK 模式**可用；CONFIG 模式下发会被拒绝。

### 命令一览（21 条）

#### 总览

| 命令 | 语法 | 说明 |
|------|------|------|
| `help`   | `help [cmd]` | 无参=分组列出全部命令；带参=该命令详细用法 |
| `status` | `status` | 系统状态总览：芯片/版本/运行时长/输入电平/3 路 PWM/温度/堆/栈/debug 开关 |

#### PWM 运行时控制（临时，不写 NVS，仅 WORK 模式）

| 命令 | 语法 | 说明 |
|------|------|------|
| `on`   | `on [ch]`                     | 开启某路/全部。`ch`=1..3 或 `all`，省略=全部 |
| `off`  | `off [ch]`                    | 关闭某路/全部（输出逻辑 0%，受 pwm_inv 映射）|
| `high` | `high [ch]`                   | 切到高电平态参数（省略 ch=全部带渐变）|
| `low`  | `low [ch]`                    | 切到低电平态参数 |
| `freq` | `freq <ch> <lo\|hi> <hz>`     | 临时改某路某态频率，重启丢失 |
| `duty` | `duty <ch> <lo\|hi> <0-1000>` | 临时改占空比，`500`=50.0%，重启丢失 |

#### 持久参数（写 NVS，仅 WORK 模式）

| 命令 | 语法 | 说明 |
|------|------|------|
| `set-freq`   | `set-freq <ch> <lo\|hi> <hz>`  | 持久设定频率 |
| `set-duty`   | `set-duty <ch> <lo\|hi> <0-1000>` | 持久设定占空比 |
| `set-rise`   | `set-rise <ms>`                | 低→高切换时间，0..60000 ms |
| `set-fall`   | `set-fall <ms>`                | 高→低切换时间，0..60000 ms |
| `set-pin`    | `set-pin <PIN>`                | 输入引脚（S3=9\|10，C3=6\|7），**需重启生效** |
| `set-invin`  | `set-invin <0\|1>`             | 输入电平反相，**需重启生效** |
| `set-pwminv` | `set-pwminv <0\|1>`            | PWM 极性反相，立即生效 |
| `set-temp`   | `set-temp <0..125>`            | 过温阈值 °C |

#### 系统操作（危险，需 `confirm`）

| 命令 | 语法 | 说明 |
|------|------|------|
| `restart`        | `restart confirm`         | 软重启 |
| `config-mode`    | `config-mode confirm`     | 重启进 WiFi 配置模式 |
| `factory-reset`  | `factory-reset confirm`   | 恢复出厂默认参数并重启 |
| `erase-nvs`      | `erase-nvs confirm`       | 擦除全部 NVS 并重启 |

#### 调试

| 命令 | 语法 | 说明 |
|------|------|------|
| `debug` | `debug <on\|off>` | 事件详细日志开关。开启后打印 PWM 切换/输入跳变/温度采样等 `[EVT]` 事件；关闭则安静 |

### 参数说明

- `<ch>`：通道号 `1` / `2` / `3`，或 `all`（部分命令可省略=全部）。
- `<lo|hi>`：`lo`=低电平态、`hi`=高电平态。
- **占空比**：`0`~`1000`，其中 `500` = 50.0%、`1000` = 100.0%、`333` = 33.3%。
- **频率**：1~40000 Hz（实用范围；<5 Hz 在 ESP32-C3 LEDC 不可达，会自降分辨率）。

### 使用示例

```
help                          # 列出全部命令
status                        # 查看状态总览
debug on                      # 开事件日志（拨动输入可见 [EVT] 行）
high 1                        # 第 1 路切高电平态（临时演示）
freq 1 hi 2000                # 临时把第 1 路高态频率改 2000 Hz
duty 1 hi 800                 # 临时把第 1 路高态占空比改 80.0%
set-duty 2 lo 300             # 第 2 路低态占空比持久设为 30.0%（写 NVS）
set-temp 75                   # 过温阈值持久设为 75 °C
restart                       # 不带 confirm → 提示需要二次确认
restart confirm               # 带 confirm → 真正软重启
config-mode confirm           # 重启进 WiFi 配置模式
factory-reset confirm         # 恢复出厂默认
debug off                     # 关事件日志
```

### 注意事项

- **物理输入优先**：CLI `high`/`low` 切换后，若输入信号电平变化，会按物理输入再切回（`status` 可看当前输入电平）。
- **过温锁定**：温度超过阈值时，PWM 运行时命令（`on/off/high/low/freq/duty`）会被拒绝（提示 `over-temp active, PWM locked`），3 路锁定 5Hz/50% 保护。
- **CONFIG 模式限制**：配置模式下 `pwm`/`set-*` 命令被拒（仅 `help/status/debug/system` 可用）。
- **set-pin / set-invin** 改后需重启生效（handler 会打印提示）。

---

## 编译

### GitHub Actions（推荐）

推送代码到 `main` 分支自动触发编译（`.github/workflows/build.yml`），使用 **ESP-IDF v5.3** 容器，矩阵编译 `esp32s3` + `esp32c3`，产物 `merged.bin` 在 Actions 运行页下载 artifact（`merged-firmware-esp32s3` / `merged-firmware-esp32c3`）。

### 本地编译

```bash
. $IDF_PATH/export.sh
idf.py set-target esp32s3      # 或 esp32c3
idf.py build
```

> S3 串口 CLI 走 USB-Serial-JTAG，需 ESP-IDF **v5.3+**（v5.2 有 USB-Serial-JTAG RX 已知 bug，见 [esp-idf#12605](https://github.com/espressif/esp-idf/issues/12605)）。

## 烧录

`merged.bin` 为合并固件（bootloader + 分区表 + 应用），单地址烧录：

```bash
# ESP32-S3
esptool.py --chip esp32s3 --port COM3 --baud 460800 write_flash 0x0 merged.bin
# ESP32-C3
esptool.py --chip esp32c3 --port COM3 --baud 460800 write_flash 0x0 merged.bin
```

（`COM3` 替换为实际串口号）

## 项目结构

```
main/
├── app.c / app.h            # 主程序入口、模式判定、引脚配置
├── cli.c / cli.h            # 串口 CLI（命令行控制 + 事件日志开关）
├── nvs_param.c / .h         # NVS 参数读写（配置持久化）
├── pwm_ctrl.c / .h          # 3 路 LEDC + 动态分辨率 + 软件渐变 + 互斥锁
├── input_sig.c / .h         # 输入信号轮询 + 消抖 + 反相
├── temp_monitor.c / .h      # DS18B20 过温保护
├── config_portal.c / .h     # SoftAP 配置页面（httpd + Captive Portal + DNS 劫持）
├── key.c / key.h            # 按键状态机（短按反馈 / 长按进配置）
└── board.h                  # 双芯片引脚映射
CMakeLists.txt               # 工程根（project(3nmos_no_wifi)）
sdkconfig.defaults           # 公共默认配置
sdkconfig.defaults.esp32s3   # S3 专属（console=USB-Serial-JTAG）
.github/workflows/build.yml  # CI（ESP-IDF v5.3，编译 C3+S3）
```

## 技术说明

- **LEDC 动态分辨率**：按目标频率自动选择 `duty_resolution`，使分频系数落在 `[2, 1024]`，覆盖 1 Hz ~ 40 kHz 并尽量提高占空比精度。
- **平滑切换（软件渐变）**：占空比变化时在调用任务里分步 `ledc_set_duty` + `vTaskDelay` 平滑过渡（每步保证 ≥1 tick，避免饿死 IDLE 看门狗）；频率变化重配 timer 并对当前 tick 做分辨率缩放消除跳变。PWM 操作由递归互斥锁串行化，消除多任务竞态。
- **占空比存储**：百分比 0.0~100.0 在 NVS 以 `uint16_t ×10` 存储（如 33.3% → 333）。
- **CLI 后端自适应**：S3 走 USB-Serial-JTAG（`usb_serial_jtag_driver_install` + `esp_vfs_usb_serial_jtag_use_driver` + `fgets`），C3 走 UART0（`uart_driver_install` + `esp_vfs_dev_uart_use_driver` + `fgets`）；TX 由 ROM console 自动适配。

## License

MIT License
