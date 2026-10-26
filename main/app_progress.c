#include "app_progress.h"

#include <string.h>

#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "py_progress";

#define NVS_NAMESPACE "pinyin"
#define NVS_KEY "state"
#define STORE_TASK_STACK 3072
#define STORE_TASK_PRIO 3
#define STORE_QUEUE_LEN 2

static pinyin_progress_t s_progress;
static QueueHandle_t s_store_queue;
static TaskHandle_t s_store_task;

static void store_task(void *arg)
{
    (void)arg;
    uint8_t blob[PROGRESS_BLOB_SIZE];
    for (;;) {
        if (xQueueReceive(s_store_queue, blob, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        /* 合并队列中更新的快照：只落盘最后一份 */
        uint8_t newer[PROGRESS_BLOB_SIZE];
        while (xQueueReceive(s_store_queue, newer, 0) == pdTRUE) {
            memcpy(blob, newer, sizeof(blob));
        }
        nvs_handle_t h;
        if (nvs_open(NVS_NAMESPACE, NVS_READWRITE, &h) != ESP_OK) {
            ESP_LOGE(TAG, "NVS 打开失败，进度未保存");
            continue;
        }
        esp_err_t err = nvs_set_blob(h, NVS_KEY, blob, sizeof(blob));
        if (err == ESP_OK) {
            err = nvs_commit(h);
        }
        nvs_close(h);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "进度保存失败: %d", err);
        }
    }
}

void pinyin_progress_open(void)
{
    pinyin_progress_defaults(&s_progress);

    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS 分区需擦除重建: %d", err);
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS 初始化失败: %d（进度将不持久化）", err);
        return;
    }

    nvs_handle_t h;
    if (nvs_open(NVS_NAMESPACE, NVS_READONLY, &h) != ESP_OK) {
        ESP_LOGI(TAG, "无历史进度，使用默认值");
    } else {
        uint8_t blob[PROGRESS_BLOB_SIZE];
        size_t len = 0;
        if (nvs_get_blob(h, NVS_KEY, blob, &len) == ESP_OK &&
            progress_decode(blob, len, &s_progress)) {
            ESP_LOGI(TAG, "进度已加载: 第%d课 卡%d",
                     s_progress.last_lesson, s_progress.last_card);
        } else {
            ESP_LOGW(TAG, "进度损坏或版本不符，使用默认值");
        }
        nvs_close(h);
    }

    s_store_queue = xQueueCreate(STORE_QUEUE_LEN, PROGRESS_BLOB_SIZE);
    if (s_store_queue != NULL) {
        xTaskCreate(store_task, "py_store", STORE_TASK_STACK, NULL,
                    STORE_TASK_PRIO, &s_store_task);
    }
}

pinyin_progress_t *pinyin_progress(void)
{
    return &s_progress;
}

void pinyin_progress_commit(void)
{
    if (s_store_queue == NULL) {
        return;
    }
    uint8_t blob[PROGRESS_BLOB_SIZE];
    if (!progress_encode(&s_progress, blob, sizeof(blob))) {
        return;
    }
    /* 队列满则丢弃最旧，保证最新快照入队 */
    if (xQueueSend(s_store_queue, blob, 0) != pdTRUE) {
        uint8_t stale[PROGRESS_BLOB_SIZE];
        xQueueReceive(s_store_queue, stale, 0);
        xQueueSend(s_store_queue, blob, 0);
    }
}
