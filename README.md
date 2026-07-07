# ESP32-C3 高频PWM控制器

基于ESP32-C3的高频PWM控制设备，支持WiFi AirKiss配网、MQTT远程控制，并通过USB CDC提供调试输出。

## 功能特性

- 📡 **WiFi配网**: 支持微信小程序AirKiss配网
- 🔌 **MQTT通信**: 连接鸿蒙系统和Home Assistant
- ⚡ **高频PWM**: GPIO3输出10kHz-300kHz可调PWM信号
- 💻 **USB CDC**: 通过USB接口输出调试日志
- 💾 **NVS存储**: 保存WiFi、MQTT配置和PWM参数
- 🔘 **按键控制**: GPIO9按键，短按切换PWM，长按5秒恢复出厂
- 🔄 **快速重启检测**: 15秒内连续上电10次自动恢复出厂设置
- 💡 **状态指示**: GPIO4 LED显示WiFi/MQTT连接状态

## 硬件要求

- ESP32-C3开发板（带USB接口）
- USB数据线（支持数据传输）

## 引脚定义

| 引脚 | 功能 | 说明 |
|------|------|------|
| GPIO3 | PWM输出 | 高频PWM信号输出（10kHz-300kHz） |
| GPIO4 | LED状态灯 | 低电平点亮，显示系统状态 |
| GPIO9 | 按键输入 | 内部上拉，短按/长按功能 |
| GPIO18 | USB D- | USB数据负（专用，不可他用） |
| GPIO19 | USB D+ | USB数据正（专用，不可他用） |

## MQTT主题

- `harmony/pwm/switch` - PWM开关控制（ON/OFF）
- `harmony/pwm/freq` - PWM频率设置（10000-300000 Hz）
- `harmony/pwm/duty` - PWM占空比设置（0-255）
- `harmony/pwm/state` - 设备状态上报

## 使用方法

### 1. 克隆仓库

```bash
git clone <your-repo-url>
cd high_frequency_esp32c3
```

### 2. 通过GitHub Actions编译（推荐）

推送代码到GitHub后，Actions会自动编译：

1. 访问 `https://github.com/<username>/high_frequency_esp32c3/actions`
2. 等待编译完成（约5-10分钟）
3. 运行下载脚本获取merged固件：
   ```powershell
   .\download_firmware.ps1
   ```
4. 固件将保存到 `downloaded_firmware/merged.bin`

### 3. 本地编译（需要ESP-IDF环境）

```bash
# 设置ESP-IDF环境
. $IDF_PATH/export.sh

# 配置目标芯片
idf.py set-target esp32c3

# 编译
idf.py build

# 烧录（通过USB）
idf.py flash monitor
```

### 4. 烧录固件

使用esptool.py烧录merged固件（推荐）：

```powershell
esptool.py --chip esp32c3 --port COM3 write_flash 0x0 downloaded_firmware\merged.bin
```

注意：将COM3替换为您的实际串口号

### 5. 查看USB CDC日志

Windows 10/11会自动识别ESP32-C3的USB CDC为串口设备：

1. 连接ESP32-C3到电脑
2. 在设备管理器中找到COM端口
3. 使用串口工具（如PuTTY、MobaXterm）打开该端口
4. 波特率任意（USB CDC虚拟串口）
5. 即可看到系统日志输出

### 6. WiFi配网

1. 确保手机连接到2.4GHz WiFi网络
2. 打开微信小程序"乐鑫AirKiss"
3. 输入WiFi密码，开始配网
4. ESP32-C3会自动连接WiFi并保存配置

### 7. MQTT配置

通过NVS保存MQTT服务器信息，首次使用需要通过以下方式配置：
- 修改代码中的默认值
- 或通过OTA更新配置
- 或使用ESP-IDF的nvs_partition_gen工具生成配置文件

## GitHub Actions自动化

本项目配置了GitHub Actions自动编译：

- 每次推送到main分支自动触发编译
- 使用ESP-IDF v5.2.1
- 目标芯片：ESP32-C3
- 自动生成merged.bin固件（包含bootloader、分区表和应用程序）
- 编译产物保留30天
- 提供PowerShell脚本一键下载固件

## 恢复出厂设置

两种方式恢复出厂设置：

1. **按键方式**: 长按GPIO9按键5秒以上
2. **快速重启**: 15秒内连续上电10次

恢复后会清除所有NVS保存的配置（WiFi、MQTT、PWM参数）。

## 技术栈

- **框架**: ESP-IDF v5.2.1
- **芯片**: ESP32-C3（RISC-V单核）
- **通信**: WiFi + MQTT
- **调试**: USB CDC（TinyUSB）
- **存储**: NVS Flash
- **PWM**: LEDC定时器

## 注意事项

1. **USB引脚**: GPIO18和GPIO19专用于USB，不能作为普通GPIO使用
2. **WiFi频段**: 仅支持2.4GHz WiFi网络
3. **PWM范围**: 频率10kHz-300kHz，占空比0-255（8位分辨率）
4. **USB驱动**: Windows 10/11通常自动识别，老系统可能需要安装驱动
5. **首次编译**: GitHub Actions首次编译较慢，后续会利用缓存加速

## 项目结构

```
├── main/                  # ESP-IDF主目录（空，兼容性保留）
├── app.c                  # 主程序入口
├── app.h                  # 主程序头文件
├── wifi_airkiss.c/h       # WiFi AirKiss配网
├── mqtt_ha_harmony.c/h    # MQTT通信
├── ledc_pwm.c/h           # LEDC PWM控制
├── nvs_param.c/h          # NVS参数存储
├── key.c/h                # 按键处理
├── rtc_boot_reset.c/h     # 快速重启检测
├── sdkconfig.defaults     # ESP-IDF默认配置
├── .github/workflows/     # GitHub Actions配置
├── setup_github_repo.sh   # GitHub仓库设置脚本
└── download_firmware.ps1  # 固件下载脚本
```

## License

MIT License

## 相关链接

- [ESP-IDF文档](https://docs.espressif.com/projects/esp-idf/)
- [ESP32-C3技术参考手册](https://www.espressif.com/sites/default/files/documentation/esp32-c3_technical_reference_manual_cn.pdf)
- [TinyUSB文档](https://docs.espressif.com/projects/esp-idf/en/latest/esp32c3/api-reference/peripherals/usb.html)
