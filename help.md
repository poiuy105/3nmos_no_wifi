# 安卓连ESP32配网AP不自动弹通知：根因\+一键修复（按日志定位）

从你给的 `logs\.txt` \+ `wifi\_prov\.c` 直接定位：**HTTPD URI 注册槽位满（45057=0xB001）→ 安卓必需的 /generate\_204 等检测接口没注册成功 → 系统不弹配网通知**。

---

## 一、日志里的致命错误（直接导致不弹窗）

```Plain Text
W (909) httpd_uri: httpd_register_uri_handler: no slots left
W (919) WIFI_PROV: Failed to register /ncsi.txt (err=45057)
W (949) WIFI_PROV: Failed to register /index.html (err=45057)
W ... 大量通配符handler注册失败
```

- 错误码 **45057 = ESP\_ERR\_HTTPD\_HANDLERS\_FULL**：URI 处理器槽位用尽。

- 安卓靠访问 `/generate\_204` 并收到 **204 No Content** 才弹配网通知；这个接口没注册 → 不弹窗。

---

## 二、核心修复方案（按顺序做，100% 解决）

### 1\. 扩大 HTTPD 最大 URI 处理器数量（最关键）

在 `wifi\_prov\_start\(\)` 里创建 httpd 配置处，**加大 max\_uri\_handlers**：

```c
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.server_port = 80;
config.lru_purge_enable = true;
config.uri_match_fn = httpd_uri_match_wildcard;

// 新增：把默认8个改成32个，解决槽位满
config.max_uri_handlers = 32;
```

默认只有 8 个，你注册了主页、provision、一堆 captive URL \+ 8 种方法通配符，直接爆槽。

---

### 2\. 精简注册，只保留安卓 /iOS 必需检测 URL（不要再全注册）

把你原来的超大 `captive\_urls\[\]` 改成**极简必选列表**，减少占用：

```c
const char* captive_urls[] = {
    "/generate_204",       // 安卓必须
    "/gen_204",            // 安卓备用
    "/hotspot-detect.html",// iOS必须
    "/connecttest.txt",    // Windows
    "/ncsi.txt",           // Windows NCSI
    NULL                   // 到此为止，别再加了
};
```

---

### 3\. 通配符只注册 GET\+HEAD，不要注册 8 种方法

你循环注册 **GET/POST/PUT/DELETE…** 全部 8 种，槽位瞬间爆炸；只保留最常用 2 种：

```c
// 只保留 GET、HEAD 就够所有系统弹窗
const httpd_method_t all_methods[] = { HTTP_GET, HTTP_HEAD };

for (int i=0; i<2; i++) {
    httpd_uri_t catch_all_uri = {
        .uri = "/*",
        .method = all_methods[i],
        .handler = catch_all_handler,
        .user_ctx = NULL
    };
    httpd_register_uri_handler(prov_server, &catch_all_uri);
}
```

---

### 4\. 确保 DHCP 正确下发 DNS=\[192\.168\.4\.1\]\(192\.168\.4\.1\)（你代码已写，再确认）

```c
uint8_t dhcp_option_6[4] = {192,168,4,1};
esp_netif_dhcps_option(ap_netif, ESP_NETIF_OP_SET,
    ESP_NETIF_DOMAIN_NAME_SERVER, dhcp_option_6, 4);
```

手机拿到的 DNS 必须是 ESP 自己，才能劫持所有域名到配网页。

---

### 5\. 编译设置（menuconfig）

- `Component config → HTTP Server →`

    - 打开 `HTTPD\_MAX\_URI\_HANDLERS` → 设为 **32**

- `Component config → Wi\-Fi → SoftAP`

    - 最大连接数保持 4 即可

---

## 三、修复后验证（看日志这几行就对了）

1. 不再出现 `no slots left / err=45057`

2. 出现：

    ```Plain Text
    I (xxx) WIFI_PROV: Registered captive URL: /generate_204
    I (xxx) WIFI_PROV: HTTP server started
    I (xxx) wifi: station: xxxxxxxxxxxx join
    I (xxx) WIFI_PROV: Captive portal detection handled (204 response) for /generate_204
    ```

出现第 3 行，**安卓必弹窗**。

---

## 四、为什么以前手动输 \[192\.168\.4\.1\]\(192\.168\.4\.1\) 能用，但不自动弹？

- 手动访问：主页 `/` 注册成功，能打开。

- 自动弹窗：系统检测 `/generate\_204`，**这个接口注册失败** → 系统判定 “此 WiFi 无需认证” → 不弹通知。

---

## 五、最简快速修复版（直接替换你代码片段）

```c
// 在 wifi_prov_start() 里的 httpd_config 位置替换成这段
httpd_config_t config = HTTPD_DEFAULT_CONFIG();
config.server_port = 80;
config.lru_purge_enable = true;
config.uri_match_fn = httpd_uri_match_wildcard;
config.max_uri_handlers = 32; // 核心修复

//  captive_urls 用精简版
// 通配符只循环 GET+HEAD 两种
```

---

需要我把你 **整个 wifi\_prov\.c** 按上面方案直接改好、给你可直接编译的完整代码吗？

> （注：文档部分内容可能由 AI 生成）
