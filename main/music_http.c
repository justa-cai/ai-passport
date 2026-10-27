// main/music_http.c —— music_http.h 的实现。
#include "music_http.h"

#include <string.h>

#include "esp_http_client.h"
#include "esp_log.h"

static const char *TAG = "music_http";

#define READ_CHUNK 2048

// net_task 单线程调用,静态读缓冲不占栈也不并发。
static char s_buf[READ_CHUNK];

static esp_http_client_config_t client_config(const char *url) {
    esp_http_client_config_t cfg = {
        .url = url,
        // 酷我全链路明文 http;误配 https 直接失败,不做降级。
        .timeout_ms = 10000,
        .keep_alive_enable = true,
        .disable_auto_redirect = false,
    };
    return cfg;
}

int music_http_get(const char *url, music_http_chunk_fn cb, void *user) {
    esp_http_client_config_t cfg = client_config(url);
    esp_http_client_handle_t c = esp_http_client_init(&cfg);
    if (!c) return -1;

    int status = -1;
    esp_err_t err = esp_http_client_open(c, 0);
    if (err == ESP_OK) {
        const int64_t len = esp_http_client_fetch_headers(c);
        if (len >= 0) status = esp_http_client_get_status_code(c);
        if (cb && status >= 200 && status < 300) {
            bool want_more = true;
            while (want_more) {
                const int n = esp_http_client_read(c, s_buf, READ_CHUNK);
                if (n <= 0) break;  // 0 = 结束;<0 = 传输错误
                if (n > 0 && !cb(s_buf, (size_t)n, user)) break;  // 收满
            }
        }
    } else {
        ESP_LOGW(TAG, "GET 打开失败:%s %s", esp_err_to_name(err), url);
    }
    // 读完剩余 body 让连接可复用;close 无论成败。
    esp_http_client_close(c);
    esp_http_client_cleanup(c);
    if (status < 0) ESP_LOGW(TAG, "GET 传输失败:%s", url);
    return status;
}
