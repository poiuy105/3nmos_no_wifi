#include "config_portal.h"
#include "esp_wifi.h"
#include "esp_http_server.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_event.h"
#include "esp_mac.h"
#include "esp_system.h"
#include "nvs_param.h"
#include "temp_monitor.h"
#include "board.h"
#include "cJSON.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "lwip/err.h"
#include "lwip/sockets.h"
#include "lwip/sys.h"
#include "lwip/netdb.h"
#include "lwip/inet.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "CFG";
static httpd_handle_t s_server = NULL;

// DNS 服务器任务（前向声明）
static void dns_server_task(void *pv);

// ---------- 辅助 ----------
static void get_mac_suffix(char *buf, size_t len)
{
    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_SOFTAP);
    snprintf(buf, len, "%02X%02X%02X", mac[3], mac[4], mac[5]);
}

static uint32_t clamp_u32(double v, uint32_t lo, uint32_t hi)
{
    if (v < (double)lo) return lo;
    if (v > (double)hi) return hi;
    return (uint32_t)v;
}

static uint16_t clamp_u16(double v, uint16_t lo, uint16_t hi)
{
    if (v < (double)lo) return lo;
    if (v > (double)hi) return hi;
    return (uint16_t)v;
}

static esp_err_t send_json_err(httpd_req_t *req, const char *msg)
{
    char buf[128];
    int n = snprintf(buf, sizeof(buf), "{\"status\":\"error\",\"message\":\"%s\"}", msg);
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

// ---------- 配置页面 HTML ----------
static const char index_html[] =
"<!DOCTYPE html><html><head><meta charset='UTF-8'>"
"<meta name='viewport' content='width=device-width,initial-scale=1'>"
"<title>3NMOS PWM</title><style>"
"body{font-family:Arial;margin:0;padding:12px;background:#eef1f5}"
"h2{text-align:center;margin:6px}"
".c{max-width:540px;margin:8px auto;background:#fff;padding:14px;border-radius:8px;box-shadow:0 1px 4px rgba(0,0,0,.12)}"
".r{display:flex;align-items:center;margin:8px 0;font-size:14px}"
".r>label{flex:0 0 150px}"
".seg label{font-weight:normal;margin:0 8px}"
"table{width:100%;border-collapse:collapse;font-size:12px;margin:6px 0}"
"td,th{border:1px solid #ddd;padding:4px;text-align:center}"
"th{background:#f3f3f3}"
"input[type=number]{width:72px;padding:5px;border:1px solid #bbb;border-radius:4px;box-sizing:border-box}"
"button{display:block;width:100%;padding:12px;margin-top:8px;border:0;border-radius:6px;color:#fff;font-size:15px}"
".b1{background:#4caf50}.b2{background:#e0463c}"
"#s{text-align:center;margin-top:8px;min-height:18px;font-size:14px}"
".ok{color:#093}.er{color:#c00}"
"</style></head><body>"
"<h2>3NMOS PWM 控制器配置</h2>"
"<div class='c'>"
"<div class='r'><label>输入信号引脚</label><span class='seg'>"
#if CONFIG_IDF_TARGET_ESP32S3
"<label><input type='radio' name='ip' value='10' checked>GPIO10</label>"
"<label><input type='radio' name='ip' value='9'>GPIO9</label></span></div>"
#else
"<label><input type='radio' name='ip' value='6' checked>GPIO6</label>"
"<label><input type='radio' name='ip' value='7'>GPIO7</label></span></div>"
#endif
"<div class='r'><label>输入电平反向</label><input type='checkbox' id='ii'></div>"
"<div class='r'><label>PWM 极性反向</label><input type='checkbox' id='pi'></div>"
"</div>"
"<div class='c'>"
"<table><tr><th>路</th><th colspan=2>低电平态</th><th colspan=2>高电平态</th></tr>"
"<tr><th></th><th>频率Hz</th><th>占空比%</th><th>频率Hz</th><th>占空比%</th></tr>"
"<tr><td>PWM1<br>(IO48)</td><td><input type=number id=f0l min=1 max=40000></td><td><input type=number id=d0l min=0 max=100 step=0.1></td><td><input type=number id=f0h min=1 max=40000></td><td><input type=number id=d0h min=0 max=100 step=0.1></td></tr>"
"<tr><td>PWM2<br>(IO21)</td><td><input type=number id=f1l min=1 max=40000></td><td><input type=number id=d1l min=0 max=100 step=0.1></td><td><input type=number id=f1h min=1 max=40000></td><td><input type=number id=d1h min=0 max=100 step=0.1></td></tr>"
"<tr><td>PWM3<br>(IO47)</td><td><input type=number id=f2l min=1 max=40000></td><td><input type=number id=d2l min=0 max=100 step=0.1></td><td><input type=number id=f2h min=1 max=40000></td><td><input type=number id=d2h min=0 max=100 step=0.1></td></tr>"
"</table></div>"
"<div class='c'>"
"<div class='r'><label>低&rarr;高 时间(ms)</label><input type=number id=tr min=0 max=65535></div>"
"<div class='r'><label>高&rarr;低 时间(ms)</label><input type=number id=tf min=0 max=65535></div>"
"</div>"
"<div class='c'>"
"<div class='r'><label>温度阈值(°C)</label><input type=number id=tthr min=0 max=125 value=100></div>"
"<div class='r'><label>当前温度</label><span id=curt>--</span> °C</div>"
"</div>"
"<button class='b1' onclick='sv()'>保存并重启</button>"
"<button class='b2' onclick='rs()'>恢复默认参数</button>"
"<div id='s'></div>"
"<script>"
"function g(id){return document.getElementById(id)}"
"function pin(){var r=document.getElementsByName('ip');for(var i=0;i<r.length;i++)if(r[i].checked)return +r[i].value;return 10}"
"function load(){fetch('/config').then(r=>r.json()).then(c=>{g('ii').checked=!!c.in_inv;g('pi').checked=!!c.pwm_inv;var r=document.getElementsByName('ip');for(var i=0;i<r.length;i++)r[i].checked=(+r[i].value===c.in_pin);for(var i=0;i<3;i++){g('f'+i+'l').value=c.ch[i].lo.f;g('d'+i+'l').value=c.ch[i].lo.d;g('f'+i+'h').value=c.ch[i].hi.f;g('d'+i+'h').value=c.ch[i].hi.d}g('tr').value=c.t_rise;g('tf').value=c.t_fall;g('tthr').value=c.t_thr;g('curt').textContent=(c.temp==null?'--':c.temp.toFixed(1))}).catch(e=>{g('s').className='er';g('s').textContent='加载失败'})}"
"function sv(){var ch=[];for(var i=0;i<3;i++)ch.push({lo:{f:+g('f'+i+'l').value,d:+g('d'+i+'l').value},hi:{f:+g('f'+i+'h').value,d:+g('d'+i+'h').value}});var cfg={in_pin:pin(),in_inv:g('ii').checked?1:0,pwm_inv:g('pi').checked?1:0,ch:ch,t_rise:+g('tr').value,t_fall:+g('tf').value,t_thr:+g('tthr').value};g('s').className='';g('s').textContent='保存中...';fetch('/save',{method:'POST',headers:{'Content-Type':'application/json'},body:JSON.stringify(cfg)}).then(r=>r.json()).then(d=>{g('s').className=d.status==='success'?'ok':'er';g('s').textContent=d.status==='success'?'已保存，设备重启中...':'失败:'+(d.message||'')}).catch(e=>{g('s').className='er';g('s').textContent='网络错误'})}"
"function rs(){if(!confirm('恢复默认参数并重启？'))return;g('s').className='';g('s').textContent='恢复中...';fetch('/reset',{method:'POST'}).then(r=>r.json()).then(d=>{g('s').className='ok';g('s').textContent='已恢复，重启中...'}).catch(e=>{g('s').className='er';g('s').textContent='网络错误'})}"
"load();"
"</script></body></html>";

// ---------- HTTP handlers ----------
static esp_err_t index_handler(httpd_req_t *req)
{
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    httpd_resp_send(req, index_html, HTTPD_RESP_USE_STRLEN);
    return ESP_OK;
}

// GET /config：返回当前 NVS 配置的 JSON
static esp_err_t config_get_handler(httpd_req_t *req)
{
    char buf[768];
    int n = snprintf(buf, sizeof(buf),
        "{\"in_pin\":%d,\"in_inv\":%d,\"pwm_inv\":%d,\"ch\":[",
        in_pin, in_inv, pwm_inv);
    for (int c = 0; c < 3 && n < (int)sizeof(buf) - 80; c++) {
        n += snprintf(buf + n, sizeof(buf) - n,
            "%s{\"lo\":{\"f\":%lu,\"d\":%u.%u},\"hi\":{\"f\":%lu,\"d\":%u.%u}}",
            c ? "," : "",
            (unsigned long)pwm_freq[c][0], pwm_duty[c][0] / 10, pwm_duty[c][0] % 10,
            (unsigned long)pwm_freq[c][1], pwm_duty[c][1] / 10, pwm_duty[c][1] % 10);
    }
    n += snprintf(buf + n, sizeof(buf) - n,
        "],\"t_rise\":%d,\"t_fall\":%d,\"t_thr\":%d,\"temp\":", t_rise_ms, t_fall_ms, temp_thresh);
    float cur_t = temp_monitor_get_temp();
    if (cur_t < -100.0f) n += snprintf(buf + n, sizeof(buf) - n, "null");
    else                 n += snprintf(buf + n, sizeof(buf) - n, "%.1f", cur_t);
    n += snprintf(buf + n, sizeof(buf) - n, "}");

    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, buf, n);
    return ESP_OK;
}

// POST /save：解析 JSON，校验，存 NVS，重启
static esp_err_t save_handler(httpd_req_t *req)
{
    int total = req->content_len;
    if (total <= 0 || total > 1024) {
        return send_json_err(req, "请求长度非法");
    }
    char *buf = malloc(total + 1);
    if (!buf) return send_json_err(req, "内存不足");

    int n = 0;
    while (n < total) {
        int r = httpd_req_recv(req, buf + n, total - n);
        if (r <= 0) break;
        n += r;
    }
    buf[n] = '\0';
    ESP_LOGI(TAG, "save body: %s", buf);

    cJSON *root = cJSON_Parse(buf);
    free(buf);
    if (!root) return send_json_err(req, "JSON 格式错误");

    cJSON *j;
    j = cJSON_GetObjectItem(root, "in_pin");  if (j) in_pin  = (j->valueint == PIN_IN2) ? PIN_IN2 : PIN_IN1;
    j = cJSON_GetObjectItem(root, "in_inv");  if (j) in_inv  = (j->valueint != 0) ? 1 : 0;
    j = cJSON_GetObjectItem(root, "pwm_inv"); if (j) pwm_inv = (j->valueint != 0) ? 1 : 0;

    cJSON *ch = cJSON_GetObjectItem(root, "ch");
    if (ch && cJSON_GetArraySize(ch) >= 3) {
        for (int c = 0; c < 3; c++) {
            cJSON *cc = cJSON_GetArrayItem(ch, c);
            cJSON *lo = cJSON_GetObjectItem(cc, "lo");
            cJSON *hi = cJSON_GetObjectItem(cc, "hi");
            if (!lo || !hi) continue;
            cJSON *jf, *jd;
            jf = cJSON_GetObjectItem(lo, "f"); if (jf) pwm_freq[c][0] = clamp_u32(jf->valuedouble, 1, 40000);
            jd = cJSON_GetObjectItem(lo, "d"); if (jd) pwm_duty[c][0]  = clamp_u16(jd->valuedouble * 10.0 + 0.5, 0, 1000);
            jf = cJSON_GetObjectItem(hi, "f"); if (jf) pwm_freq[c][1] = clamp_u32(jf->valuedouble, 1, 40000);
            jd = cJSON_GetObjectItem(hi, "d"); if (jd) pwm_duty[c][1]  = clamp_u16(jd->valuedouble * 10.0 + 0.5, 0, 1000);
        }
    }

    j = cJSON_GetObjectItem(root, "t_rise"); if (j) t_rise_ms = clamp_u16(j->valuedouble, 0, 65535);
    j = cJSON_GetObjectItem(root, "t_fall"); if (j) t_fall_ms = clamp_u16(j->valuedouble, 0, 65535);
    j = cJSON_GetObjectItem(root, "t_thr");
    if (j) {
        int v = (int)j->valuedouble;
        if (v < 0) v = 0;
        if (v > 125) v = 125;
        temp_thresh = v;
    }

    cJSON_Delete(root);

    cfg_ok = 1;
    nvs_save_all_param();

    const char *resp = "{\"status\":\"success\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));

    ESP_LOGI(TAG, "config saved, restarting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// POST /reset：恢复默认参数并重启（cfg_ok=0 → 重启后自动进配置页）
static esp_err_t reset_handler(httpd_req_t *req)
{
    nvs_load_defaults();
    nvs_save_all_param();
    const char *resp = "{\"status\":\"success\"}";
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, resp, strlen(resp));
    ESP_LOGI(TAG, "reset to defaults, restarting...");
    vTaskDelay(pdMS_TO_TICKS(1000));
    esp_restart();
    return ESP_OK;
}

// Captive Portal：已知检测 URL 返回 204
static esp_err_t captive_handler(httpd_req_t *req)
{
    // 返回 302 重定向到配置页：让系统认为"需要网页登录" → 弹出 captive portal 通知
    // （返回 204 等于告诉系统"能上网"，反而不弹）
    httpd_resp_set_status(req, "302 Found");
    httpd_resp_set_hdr(req, "Location", "/");
    httpd_resp_send(req, NULL, 0);
    return ESP_OK;
}

// 通配符：检测URL/POST 返 204；其他 GET 重定向到 /
static esp_err_t catch_all_handler(httpd_req_t *req)
{
    const char *det[] = {"/generate_204", "/gen_204", "/hotspot-detect.html",
                         "/connecttest.txt", "/ncsi.txt", "/favicon.ico", NULL};
    bool is_det = false;
    for (int i = 0; det[i]; i++) {
        if (strcmp(req->uri, det[i]) == 0) { is_det = true; break; }
    }
    if (is_det || req->method == HTTP_POST) {
        httpd_resp_set_status(req, "204 No Content");
        httpd_resp_send(req, NULL, 0);
    } else {
        httpd_resp_set_status(req, "302 Found");
        httpd_resp_set_hdr(req, "Location", "/");
        httpd_resp_send(req, NULL, 0);
    }
    return ESP_OK;
}

// ---------- DNS 劫持任务（所有域名 -> 192.168.4.1）----------
static void dns_server_task(void *pv)
{
    struct sockaddr_in dest = {0};
    dest.sin_family      = AF_INET;
    dest.sin_port        = htons(53);
    dest.sin_addr.s_addr = htonl(INADDR_ANY);

    int sock = socket(AF_INET, SOCK_DGRAM, IPPROTO_IP);
    if (sock < 0) { ESP_LOGE(TAG, "dns socket fail"); vTaskDelete(NULL); return; }

    int opt = 1;
    setsockopt(sock, SOL_SOCKET, SO_REUSEADDR, &opt, sizeof(opt));
    if (bind(sock, (struct sockaddr *)&dest, sizeof(dest)) < 0) {
        ESP_LOGE(TAG, "dns bind fail"); closesocket(sock); vTaskDelete(NULL); return;
    }
    ESP_LOGI(TAG, "DNS hijack listening on 0.0.0.0:53");

    struct timeval to = { .tv_sec = 10, .tv_usec = 0 };
    setsockopt(sock, SOL_SOCKET, SO_RCVTIMEO, &to, sizeof(to));

    char rx[512];
    struct sockaddr_in src;
    socklen_t slen = sizeof(src);
    while (1) {
        int len = recvfrom(sock, rx, sizeof(rx), 0, (struct sockaddr *)&src, &slen);
        if (len <= 0) continue;
        ESP_LOGI(TAG, "DNS query from %s (%d bytes)", inet_ntoa(src.sin_addr), len);

        char resp[512];
        memcpy(resp, rx, len);
        resp[2] |= 0x80;   // QR=1
        resp[3] |= 0x80;   // RA=1
        resp[6]  = 0; resp[7] = 1;   // ANCOUNT=1

        int rl = len;
        resp[rl++] = 0xC0; resp[rl++] = 0x0C;   // 指针指向问题
        resp[rl++] = 0;    resp[rl++] = 1;      // TYPE A
        resp[rl++] = 0;    resp[rl++] = 1;      // CLASS IN
        resp[rl++] = 0;    resp[rl++] = 0; resp[rl++] = 0; resp[rl++] = 60;  // TTL 60
        resp[rl++] = 0;    resp[rl++] = 4;      // RDLEN 4
        resp[rl++] = 192; resp[rl++] = 168; resp[rl++] = 4; resp[rl++] = 1;  // 192.168.4.1
        sendto(sock, resp, rl, 0, (struct sockaddr *)&src, slen);
    }
}

// ---------- 启动 ----------
void config_portal_start(void)
{
    char mac[16], ap_ssid[40];
    get_mac_suffix(mac, sizeof(mac));
    snprintf(ap_ssid, sizeof(ap_ssid), "3NMOS-%s", mac);

    ESP_LOGI(TAG, "========================================");
    ESP_LOGI(TAG, " 配置模式：连接 WiFi \"%s\"（无密码）", ap_ssid);
    ESP_LOGI(TAG, " 浏览器自动弹出页面，或访问 http://192.168.4.1");
    ESP_LOGI(TAG, "========================================");

    esp_netif_init();
    esp_event_loop_create_default();
    esp_netif_create_default_wifi_ap();

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    wifi_config_t ap = {0};
    ap.ap.channel       = 1;
    ap.ap.max_connection= 4;
    ap.ap.authmode      = WIFI_AUTH_OPEN;
    strncpy((char *)ap.ap.ssid, ap_ssid, sizeof(ap.ap.ssid) - 1);
    ap.ap.ssid_len = strlen(ap_ssid);
    ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_AP, &ap));
    ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_AP));
    ESP_ERROR_CHECK(esp_wifi_start());

    // DHCP：DNS 指向自己
    esp_netif_t *ni = esp_netif_get_handle_from_ifkey("WIFI_AP_DEF");
    if (ni) {
        esp_netif_dhcps_stop(ni);
        esp_netif_ip_info_t ip;
        IP4_ADDR(&ip.ip, 192, 168, 4, 1);
        IP4_ADDR(&ip.gw, 192, 168, 4, 1);
        IP4_ADDR(&ip.netmask, 255, 255, 255, 0);
        esp_netif_set_ip_info(ni, &ip);
        uint8_t dns_opt[4] = {192, 168, 4, 1};
        esp_netif_dhcps_option(ni, ESP_NETIF_OP_SET, ESP_NETIF_DOMAIN_NAME_SERVER, dns_opt, sizeof(dns_opt));
        esp_netif_dhcps_start(ni);
    }

    xTaskCreate(dns_server_task, "dns", 4096, NULL, 5, NULL);

    httpd_config_t config = HTTPD_DEFAULT_CONFIG();
    config.server_port     = 80;
    config.lru_purge_enable= true;
    config.uri_match_fn    = httpd_uri_match_wildcard;
    config.max_uri_handlers= 16;
    if (httpd_start(&s_server, &config) != ESP_OK) {
        ESP_LOGE(TAG, "httpd start failed");
        return;
    }

    httpd_register_uri_handler(s_server, &((httpd_uri_t){.uri="/",       .method=HTTP_GET,  .handler=index_handler}));
    httpd_register_uri_handler(s_server, &((httpd_uri_t){.uri="/config", .method=HTTP_GET,  .handler=config_get_handler}));
    httpd_register_uri_handler(s_server, &((httpd_uri_t){.uri="/save",   .method=HTTP_POST, .handler=save_handler}));
    httpd_register_uri_handler(s_server, &((httpd_uri_t){.uri="/reset",  .method=HTTP_POST, .handler=reset_handler}));

    const char *captive[] = {"/generate_204", "/gen_204", "/hotspot-detect.html",
                             "/connecttest.txt", "/ncsi.txt", NULL};
    for (int i = 0; captive[i]; i++) {
        httpd_register_uri_handler(s_server, &((httpd_uri_t){.uri=captive[i], .method=HTTP_GET, .handler=captive_handler}));
    }
    const httpd_method_t methods[] = {HTTP_GET, HTTP_HEAD, HTTP_POST};
    for (int i = 0; i < 3; i++) {
        httpd_register_uri_handler(s_server, &((httpd_uri_t){.uri="/*", .method=methods[i], .handler=catch_all_handler}));
    }
}
