// main/music_httpd.c —— music_httpd.h 的实现。
//
// 两个页面刻意保持单文件内联 HTML(各约 2-3KB),避免 gzip/资产管线。
// 配网页:表单 application/x-www-form-urlencoded,收满 Content-Length
// (>512 拒收)后一次性解析,每秒轮询 /api/status 直到上线。
// 遥控页:2s 轮询 /api/status(整包队列快照),命令走 POST /api/cmd
// (play/pause/next/prev/vol/chart)与 GET /api/search?q=。
// 遥控命令与状态由 main 注入(httpd 不持有队列/播放器);回调在 httpd
// 任务上下文执行,main 侧用 LVGL 锁保护共享状态。
#include "music_httpd.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "esp_http_server.h"
#include "esp_log.h"

#include "music_wifi.h"

static const char *TAG = "music_httpd";

static httpd_handle_t s_server;
static bool s_server_sta;   // 在跑的服务是遥控页(true)还是配网页

static music_remote_cmd_fn s_cmd_fn;
static music_remote_status_fn s_status_fn;
static void *s_remote_user;

// ---- 表单/查询解码 --------------------------------------------------------

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

// 从 "a=1&b=2" 里取字段值并 %XX/'+' 解码到 out(NUL 结尾)。
// body 只读不做任何就地修改(就地终止会把后面的字段一起毁掉);
// 返回 false = 未找到/越界/非法编码。*out_len 为解码后长度
// (调用方据此拒绝内部 NUL,即 %00)。
static bool form_field(const char *body, const char *name,
                       char *out, size_t out_cap, size_t *out_len) {
    const size_t nlen = strlen(name);
    const char *p = body;
    while (p) {
        const char *eq = strchr(p, '=');
        if (!eq) return false;
        const char *amp = strchr(eq, '&');
        const size_t vlen_raw = (size_t)((amp ? amp : eq + strlen(eq)) - (eq + 1));
        if ((size_t)(eq - p) == nlen && memcmp(p, name, nlen) == 0) {
            if (vlen_raw >= out_cap) return false;
            memcpy(out, eq + 1, vlen_raw);
            const int vlen = urldecode(out, vlen_raw);
            if (vlen < 0) return false;
            out[vlen] = '\0';
            *out_len = (size_t)vlen;
            return true;
        }
        p = amp ? amp + 1 : NULL;
    }
    return false;
}

// 收一个有上限的请求体;返回长度,-1 = 收包失败/超长。
static int recv_body(httpd_req_t *req, char *body, size_t cap) {
    const size_t want = req->content_len;
    if (want == 0 || want > cap - 1) return -1;
    size_t got = 0;
    while (got < want) {
        const int n = httpd_req_recv(req, body + got, (size_t)(want - got));
        if (n <= 0) return -1;
        got += (size_t)n;
    }
    body[got] = '\0';
    return (int)got;
}

static void send_json(httpd_req_t *req, const char *json) {
    httpd_resp_set_type(req, "application/json");
    httpd_resp_send(req, json, HTTPD_RESP_USE_STRLEN);
}

// ---- 配网页(AP 模式) -----------------------------------------------------

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

static esp_err_t handler_root_ap(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE_PROVISION, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_wifi_post(httpd_req_t *req) {
    char body[513];
    if (recv_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }

    char ssid[65];
    char pass[65];
    size_t ssid_len = 0, pass_len = 0;
    // 字段解码后要能按 C 字符串交给校验:内部再无 NUL。
    if (!form_field(body, "ssid", ssid, sizeof(ssid), &ssid_len) ||
        !form_field(body, "pass", pass, sizeof(pass), &pass_len) ||
        memchr(ssid, '\0', ssid_len) || memchr(pass, '\0', pass_len)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad form");
        return ESP_FAIL;
    }

    if (!music_wifi_provision(ssid, pass)) {
        send_json(req, "{\"ok\":false,\"msg\":\"名称或密码长度不合法\"}");
        return ESP_OK;
    }
    send_json(req, "{\"ok\":true}");
    return ESP_OK;
}

static esp_err_t handler_status_ap(httpd_req_t *req) {
    // 状态由 music_wifi 查询;AP 模式下只会是 ap_wait/connecting。
    static const char *TABLE[] = { "ap_wait", "connecting", "online" };
    const char *s = TABLE[music_wifi_state()];
    char json[48];
    const int n = snprintf(json, sizeof(json), "{\"state\":\"%s\"}", s);
    httpd_resp_set_type(req, "application/json");
    return httpd_resp_send(req, json, n);
}

// ---- 遥控页(STA 模式) ---------------------------------------------------

static const char PAGE_REMOTE[] =
    "<!DOCTYPE html><html><head><meta charset=\"utf-8\">"
    "<meta name=\"viewport\" content=\"width=device-width,initial-scale=1\">"
    "<title>musicdl 遥控</title><style>"
    "body{font-family:sans-serif;background:#0e1116;color:#e8edf2;"
    "max-width:420px;margin:0 auto;padding:16px}"
    "h1{font-size:20px;color:#34d399;margin:0 0 12px}"
    ".card{background:#161b22;border-radius:8px;padding:12px;margin-bottom:12px}"
    "#np{font-size:17px;white-space:nowrap;overflow:hidden;text-overflow:ellipsis}"
    "#na{color:#8b98a5;font-size:13px;margin-top:2px}"
    "#bar{height:6px;background:#1f2733;border-radius:3px;margin-top:8px}"
    "#fill{height:6px;background:#34d399;border-radius:3px;width:0}"
    "#pst{color:#8b98a5;font-size:12px;margin-top:6px;display:flex;"
    "justify-content:space-between}"
    "button{background:#161b22;color:#e8edf2;border:1px solid #1f2733;"
    "border-radius:6px;padding:8px 0;flex:1;margin:0 4px;font-size:15px}"
    "#ctl{display:flex}input[type=range]{width:100%;margin-top:8px}"
    "#sf{display:flex;gap:8px}input[type=text]{flex:1;padding:10px;"
    "border:1px solid #1f2733;border-radius:6px;background:#0e1116;"
    "color:#e8edf2;box-sizing:border-box}"
    "#sf button{flex:0 0 72px;padding:10px 0;margin:0;background:#34d399;"
    "color:#0e1116;border:0}"
    ".row{display:flex;gap:8px;padding:8px 4px;border-bottom:1px solid #1f2733;"
    "cursor:pointer}.row div{flex:1;white-space:nowrap;overflow:hidden;"
    "text-overflow:ellipsis}.row span{color:#8b98a5;font-size:12px;flex:0 0 auto}"
    ".chips{display:flex;flex-wrap:wrap;gap:8px;margin-top:10px}"
    ".chips button{flex:0 0 auto;padding:6px 10px;margin:0;font-size:13px}"
    "</style></head><body><h1>musicdl 遥控</h1>"
    "<div class=card><div id=np>未播放</div><div id=na></div>"
    "<div id=bar><div id=fill></div></div>"
    "<div id=pst><span id=ps></span><span id=tm></span></div></div>"
    "<div class=card id=ctl><button onclick=cmd('prev')>上一首</button>"
    "<button onclick=cmd('pause')>暂停</button>"
    "<button onclick=cmd('next')>下一首</button></div>"
    "<div class=card>音量<input type=range min=0 max=100 value=70 "
    "onchange=\"cmd('vol',this.value)\"></div>"
    "<div class=card><form id=sf onsubmit=\"return doSearch()\">"
    "<input type=text placeholder=\"歌曲 / 歌手\" maxlength=32>"
    "<button>搜索</button></form><div class=chips id=ch></div></div>"
    "<div class=card id=list></div><script>"
    "const CH=[['热歌榜',16],['新歌榜',17],['华语榜',62],['飙升榜',93],"
    "['短视频',158]];"
    "function cmd(c,v){fetch('/api/cmd',{method:'POST',headers:{'Content-Type':"
    "'application/x-www-form-urlencoded'},body:'cmd='+c+(v!==undefined?"
    "'&value='+v:'')}).then(()=>poll())}"
    "function doSearch(){const e=document.querySelector('#sf input');"
    "const q=encodeURIComponent(e.value);if(!q)return false;"
    "fetch('/api/search?q='+q).then(()=>setTimeout(poll,800));return false}"
    "function mm(s){return s?Math.floor(s/60)+':'+String(s%60).padStart(2,'0'):"
    "'0:00'}"
    "function poll(){fetch('/api/status').then(r=>r.json()).then(j=>{"
    "np.textContent=j.title||'未播放';na.textContent=j.artist||'';"
    "fill.style.width=(j.pos&&j.dur?j.pos*100/j.dur:0)+'%';"
    "ps.textContent=j.ps;tm.textContent=mm(j.pos)+' / '+mm(j.dur);"
    "const L=document.getElementById('list');L.innerHTML='';"
    "(j.list||[]).forEach((s,i)=>{const r=document.createElement('div');"
    "r.className='row';const t=document.createElement('div');"
    "t.textContent=(i+1)+'. '+s[0];const a=document.createElement('span');"
    "a.textContent=s[1];r.appendChild(t);r.appendChild(a);"
    "if(i===j.idx)r.style.color='#34d399';r.onclick=()=>cmd('play',i);"
    "L.appendChild(r)});"
    "const C=document.getElementById('ch');if(!C.childElementCount)"
    "CH.forEach(c=>{const b=document.createElement('button');b.textContent=c[0];"
    "b.onclick=()=>cmd('chart',c[1]);C.appendChild(b)})})}"
    "setInterval(poll,2000);poll();</script></body></html>";

static esp_err_t handler_root_sta(httpd_req_t *req) {
    httpd_resp_set_type(req, "text/html; charset=utf-8");
    return httpd_resp_send(req, PAGE_REMOTE, HTTPD_RESP_USE_STRLEN);
}

static esp_err_t handler_status_sta(httpd_req_t *req) {
    if (!s_status_fn) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no app");
        return ESP_FAIL;
    }
    // 整包队列快照可达数 KB,放堆上;2s 轮询一次的频率无所谓开销。
    char *json = malloc(MUSIC_HTTPD_STATUS_CAP);
    if (!json) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "oom");
        return ESP_FAIL;
    }
    const size_t n = s_status_fn(json, MUSIC_HTTPD_STATUS_CAP, s_remote_user);
    httpd_resp_set_type(req, "application/json");
    const esp_err_t err = httpd_resp_send(req, json, (ssize_t)n);
    free(json);
    return err;
}

// GET /api/search?q=关键词(UTF-8 百分号编码)。校验后交 main 决定来源切换。
static esp_err_t handler_search(httpd_req_t *req) {
    const char *q = strchr(req->uri, '?');
    if (!q || strncmp(q, "?q=", 3) != 0 || !s_cmd_fn) {
        send_json(req, "{\"ok\":false}");
        return ESP_OK;
    }
    char kw[130];   // 64 字节关键词的编码形式至多 3 倍膨胀,留裕量
    const size_t raw = strlen(q + 3);
    if (raw == 0 || raw >= sizeof(kw)) {
        send_json(req, "{\"ok\":false}");
        return ESP_OK;
    }
    memcpy(kw, q + 3, raw + 1);
    const int len = urldecode(kw, raw);
    if (len <= 0 || len > 64 || memchr(kw, '\0', (size_t)len)) {
        send_json(req, "{\"ok\":false}");
        return ESP_OK;
    }
    send_json(req, s_cmd_fn("search", 0, kw, s_remote_user)
                   ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}

// POST /api/cmd:cmd=play|pause|next|prev|vol|chart & value=N。
static esp_err_t handler_cmd_post(httpd_req_t *req) {
    if (!s_cmd_fn) {
        httpd_resp_send_err(req, HTTPD_500_INTERNAL_SERVER_ERROR, "no app");
        return ESP_FAIL;
    }
    char body[129];
    if (recv_body(req, body, sizeof(body)) < 0) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad length");
        return ESP_FAIL;
    }
    char cmd[9];
    char val[9];
    size_t cmd_len = 0, val_len = 0;
    if (!form_field(body, "cmd", cmd, sizeof(cmd), &cmd_len) || cmd_len == 0 ||
        memchr(cmd, '\0', cmd_len)) {
        httpd_resp_send_err(req, HTTPD_400_BAD_REQUEST, "bad form");
        return ESP_FAIL;
    }

    int value = 0;
    if (form_field(body, "value", val, sizeof(val), &val_len) && val_len > 0 &&
        !memchr(val, '\0', val_len)) {
        char *end = NULL;
        const long v = strtol(val, &end, 10);
        if (end != val && *end == '\0') value = (int)v;
    }
    send_json(req, s_cmd_fn(cmd, value, NULL, s_remote_user)
                   ? "{\"ok\":true}" : "{\"ok\":false}");
    return ESP_OK;
}

// ---- 生命周期 ---------------------------------------------------------------

static httpd_handle_t start_server(const httpd_uri_t *uris, size_t count) {
    httpd_handle_t server = NULL;
    httpd_config_t cfg = HTTPD_DEFAULT_CONFIG();
    cfg.max_open_sockets = 4;     // 内存紧,配网页/遥控页并发需求低
    cfg.stack_size = 6144;
    if (httpd_start(&server, &cfg) != ESP_OK) {
        ESP_LOGE(TAG, "HTTP 服务启动失败");
        return NULL;
    }
    for (size_t i = 0; i < count; i++) {
        httpd_register_uri_handler(server, &uris[i]);
    }
    return server;
}

void music_httpd_start_ap(void) {
    if (s_server && !s_server_sta) return;
    music_httpd_stop();
    static const httpd_uri_t URIS[] = {
        { .uri = "/", .method = HTTP_GET, .handler = handler_root_ap },
        { .uri = "/api/wifi", .method = HTTP_POST, .handler = handler_wifi_post },
        { .uri = "/api/status", .method = HTTP_GET, .handler = handler_status_ap },
    };
    s_server = start_server(URIS, sizeof(URIS) / sizeof(URIS[0]));
    s_server_sta = false;
    if (s_server) ESP_LOGI(TAG, "配网页就绪:http://192.168.4.1/");
}

void music_httpd_start_sta(void) {
    if (s_server && s_server_sta) return;
    music_httpd_stop();
    static const httpd_uri_t URIS[] = {
        { .uri = "/", .method = HTTP_GET, .handler = handler_root_sta },
        { .uri = "/api/status", .method = HTTP_GET, .handler = handler_status_sta },
        { .uri = "/api/search", .method = HTTP_GET, .handler = handler_search },
        { .uri = "/api/cmd", .method = HTTP_POST, .handler = handler_cmd_post },
    };
    s_server = start_server(URIS, sizeof(URIS) / sizeof(URIS[0]));
    s_server_sta = true;
    if (s_server) ESP_LOGI(TAG, "遥控页就绪:局域网内设备 IP 的 80 端口(无鉴权)");
}

void music_httpd_stop(void) {
    if (!s_server) return;
    httpd_stop(s_server);
    s_server = NULL;
    ESP_LOGI(TAG, "HTTP 服务已停止");
}

void music_httpd_set_remote(music_remote_cmd_fn cmd, music_remote_status_fn status,
                            void *user) {
    s_cmd_fn = cmd;
    s_status_fn = status;
    s_remote_user = user;
}
