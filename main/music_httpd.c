// main/music_httpd.c —— music_httpd.h 的实现。
//
// 配网页刻意保持单文件内联 HTML(约 2KB),避免 gzip/资产管线;表单用
// application/x-www-form-urlencoded,收满 Content-Length(>512 拒收)后
// 一次性解析。页面每秒轮询 /api/status,连上后提示成功。
#include "music_httpd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "music_wifi.h"

static const char *TAG = "music_httpd";

static httpd_handle_t s_server;

// ---- 表单解码 -------------------------------------------------------------

// 就地把 %XX 与 '+' 解码,输出永远 <= 输入;返回解码后长度,越界返回 -1。
static int urldecode(char *s, size_t len) {
    size_t out = 0;
    for (size_t i = 0; i < len; i++) {
        if (s[i] == '+') {
            s[out++] = ' ';
        } else if (s[i] == '%' && i + 2 < len) {
            const char hex[3] = { s[i + 1], s[i + 2], 0 };
            char *end = NULL;
            const long v = strtol(hex, &end, 16);
            if (end != hex + 2) return -1;
            s[out++] = (char)v;
            i += 2;
        } else {
            s[out++] = s[i];
        }
    }
    return (int)out;
}

// 从 "ssid=..&pass=.." 里取解码后的字段值;找不到返回 NULL。
// body 会被就地修改(解码),每字段只找一次。
static char *form_field(char *body, const char *name, size_t *out_len) {
    const size_t nlen = strlen(name);
    char *p = body;
    while (p) {
        char *eq = strchr(p, '=');
        if (!eq) return NULL;
        char *amp = strchr(eq, '&');
        const size_t vlen_raw = (size_t)((amp ? amp : eq + strlen(eq)) - (eq + 1));
        if ((size_t)(eq - p) == nlen && memcmp(p, name, nlen) == 0) {
            const int vlen = urldecode(eq + 1, vlen_raw);
            if (vlen < 0) return NULL;
            if (amp) *amp = '\0';
            *out_len = (size_t)vlen;
            return eq + 1;
        }
        p = amp ? amp + 1 : NULL;
    }
    return NULL;
}

// ---- handlers -------------------------------------------------------------

static const char PAGE_PROVISION[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>AI Passport 配网</title><style>"
    "body{font-family:sans-serif;background:#0e1116;color:#e8edf2;"
    "max-width:320px;margin:40px auto;padding:0 16px}"
    "h1{font-size:20px;color:#34d399}input{width:100%;padding:10px;margin:6px 0;"
    "border:1px solid #1f2733;border-radius:6px;background:#161b22;color:#e8edf2;"
    "box-sizing:border-box}button{width:100%;padding:12px;margin-top:12px;"
    "background:#34d399;border:0;border-radius:6px;color:#0e1116;font-size:16px}"
    "#st{margin-top:16px;color:#8b98a5}</style></head><body>"
    "<h1>musicdl 播放器配网</h1>"
    "<p>选择家里 Wi-Fi 并输入密码,提交后设备自动连接。</p>"
    "<input id=\"ssid\" placeholder=\"Wi-Fi 名称\" maxlength=\"32\">"
    "<input id=\"pass\" type=\"password\" placeholder=\"密码(至少 8 位)\" maxlength=\"64\">"
    "<button onclick=\"go()\">连接</button><p id=\"st\"></p><script>"
    "function go(){fetch('/api/wifi',{method:'POST',"
    "headers:{'Content-Type':'application/x-www-form-urlencoded'},"
    "body:'ssid='+encodeURIComponent(ssid.value)+'&pass='+"
    "encodeURIComponent(pass.value)}).then(r=>{st.textContent=r.ok?"
    "'已提交,正在连接...':'提交失败:请检查名称与密码长度';poll()})"
    ".catch(()=>st.textContent='提交失败,请重试')}"
    "function poll(){setInterval(()=>fetch('/api/status').then(r=>r.json())"
    ".then(j=>{if(j.state==='online'){st.textContent='连接成功,可以关闭本页';"
    "clearInterval(poll)}}),1000)}</script></body></html>";

static esp_err_t handler_root(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE_PROVISION, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_wifi_post(httpd_req_t *req) {
    char body[513];
    const size_t want = req->content_len;
    if (want == 0 || want > 512) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }
    size_t got = 0;
    while (got < want) {
        const int n = httpd_req_recv(req, body + got, (size_t)(want - got));
        if (n <= 0) {
            httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "recv");
            return ESP_FAIL;
        }
        got += (size_t)n;
    }
    body[got] = '\0';

    size_t ssid_len = 0, pass_len = 0;
    char *ssid = form_field(body, "ssid", &ssid_len);
    char *pass = ssid ? form_field(body, "pass", &pass_len) : NULL;
    // 字段解码后要能按 C 字符串交给校验:内部再无 NUL。
    if (!ssid || !pass || memchr(ssid, '\0', ssid_len) ||
        memchr(pass, '\0', pass_len)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad form");
        return ESP_FAIL;
    }
    ssid[ssid_len] = '\0';
    pass[pass_len] = '\0';

    if (!music_wifi_provision(ssid, pass)) {
        httpd_resp_set_type(req, "application/json");
        return httpd_resp_send(req,
            "{\"ok\":false,\"msg\":\"名称或密码长度不合法\"}",
            HTTPD_RESP_USE_STRLEN);
    }
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, "{\"ok\":true}", HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_status(httpd_req_t *req) {
    // 状态由 music_wifi 查询;AP 模式下只会是 ap_wait/connecting。
    static const char *TABLE[] = { "ap_wait", "connecting", "online" };
    const char *s = TABLE[music_wifi_state()];
    char json[48];
    const int n = snprintf(json, sizeof(json), "{\"state\":\"%s\"}", s);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, n);
}

// ---- 生命周期 -------------------------------------------------------------

void music_httpd_start_ap(void) {
    if (s_server) return;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = 4;     // 内存紧,配网页并发需求低
    cfg.stack_size = 6144;
    static const httpd_uri_t root = { .uri = "/", .method = HTTP_GET,
                                      .handler = handler_root };
    static const httpd_uri_t wifi_post = { .uri = "/api/wifi",
                                           .method = HTTP_POST,
                                           .handler = handler_wifi_post };
    static const httpd_uri_t status = { .uri = "/api/status", .method = HTTP_GET,
                                        .handler = handler_status };
    if (httpd_start(&s_server, &cfg) != ESP_OK) {
        s_server = NULL;
        ESP_LOGE(TAG, "HTTP 服务启动失败");
        return;
    }
    httpd_register_uri_handler(s_server, &root);
    httpd_register_uri_handler(s_server, &wifi_post);
    httpd_register_uri_handler(s_server, &status);
    ESP_LOGI(TAG, "配网页就绪:http://192.168.4.1/");
}

void music_httpd_stop(void) {
    if (!s_server) return;
    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "HTTP 服务已停止");
}
