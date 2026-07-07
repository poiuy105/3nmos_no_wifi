#ifndef CONFIG_PORTAL_H
#define CONFIG_PORTAL_H

// 启动 AP 配置页面（SoftAP + httpd + Captive Portal + DNS 劫持）。
// 阻塞调用：保存配置或恢复默认后内部 esp_restart()，不会返回。
void config_portal_start(void);

#endif
