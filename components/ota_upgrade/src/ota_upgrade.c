#include "ota_upgrade.h"
#include "device_id.h"
#include "mqtt_bridge.h"
#include "status_led.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_http_client.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include <string.h>
#include <stdlib.h>

static const char *TAG = "ota_upgrade";

static char ota_notify_topic[128] = {0};
static char ota_progress_topic[128] = {0};
static char ota_result_topic[128] = {0};

static volatile int ota_status = 0;
static volatile bool ota_in_progress = false;
static volatile int ota_progress = 0;

typedef struct {
    char url[256];
    char md5[33];
    int task_id;
} ota_task_params_t;

static void ota_task(void *pvParameters);
static bool on_ota_data(const char *topic, int topic_len, const char *data, int data_len);

#define publish(t, p, r) mqtt_bridge_publish(t, p, r)

esp_err_t ota_upgrade_init(void)
{
    snprintf(ota_notify_topic, sizeof(ota_notify_topic),
             "device/%s/ota/notify", base_device_id);
    snprintf(ota_progress_topic, sizeof(ota_progress_topic),
             "device/%s/ota/progress", base_device_id);
    snprintf(ota_result_topic, sizeof(ota_result_topic),
             "device/%s/ota/result", base_device_id);

    mqtt_bridge_add_data_cb(on_ota_data);
    mqtt_bridge_subscribe(ota_notify_topic, 1);

    ESP_LOGI(TAG, "OTA notify topic: %s", ota_notify_topic);

    return ESP_OK;
}

static bool on_ota_data(const char *topic, int topic_len,
                        const char *data, int data_len)
{
    if (topic_len != (int)strlen(ota_notify_topic) ||
        strncmp(topic, ota_notify_topic, topic_len) != 0)
        return false;

    cJSON *root = cJSON_Parse(data);
    if (!root) { ESP_LOGE(TAG, "Failed to parse OTA JSON"); return true; }

    cJSON *url_json = cJSON_GetObjectItem(root, "url");
    cJSON *md5_json = cJSON_GetObjectItem(root, "md5");
    cJSON *task_id_json = cJSON_GetObjectItem(root, "task_id");

    if (url_json && cJSON_IsString(url_json)) {
        if (ota_in_progress) {
            ESP_LOGW(TAG, "OTA already in progress");
            cJSON_Delete(root);
            return true;
        }
        ota_task_params_t *params = calloc(1, sizeof(ota_task_params_t));
        if (!params) { cJSON_Delete(root); return true; }
        strncpy(params->url, url_json->valuestring, sizeof(params->url) - 1);
        if (md5_json && cJSON_IsString(md5_json))
            strncpy(params->md5, md5_json->valuestring, sizeof(params->md5) - 1);
        params->task_id = task_id_json ? task_id_json->valueint : 0;

        ota_in_progress = true;
        if (xTaskCreate(ota_task, "ota_task", 6144, params, 5, NULL) != pdPASS) {
            ESP_LOGE(TAG, "Failed to create OTA task");
            ota_in_progress = false;
            free(params);
        }
    }
    cJSON_Delete(root);
    return true;
}

int ota_upgrade_get_status(void) { return ota_status; }
int ota_upgrade_get_progress(void) { return ota_progress; }

static void report_progress(int progress)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "progress", progress);
    cJSON_AddNumberToObject(root, "timestamp", esp_timer_get_time() / 1000000);
    char *payload = cJSON_PrintUnformatted(root);
    if (payload) { publish(ota_progress_topic, payload, 0); cJSON_free(payload); }
    cJSON_Delete(root);
}

static void report_result(bool success, const char *error_msg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", success);
    cJSON_AddStringToObject(root, "device_id", base_device_id);
    cJSON_AddStringToObject(root, "firmware_version", FIRMWARE_VERSION);
    if (error_msg) cJSON_AddStringToObject(root, "error", error_msg);
    char *payload = cJSON_PrintUnformatted(root);
    if (payload) { publish(ota_result_topic, payload, 0); cJSON_free(payload); }
    cJSON_Delete(root);
}

static void ota_task(void *pvParameters)
{
    ota_task_params_t *params = (ota_task_params_t *)pvParameters;

    ESP_LOGI(TAG, "Starting OTA: %s", params->url);
    report_progress(0);
    ota_status = 1;
    status_led_set_state(STATUS_LED_OTA_UPGRADE);

    const esp_partition_t *update_part = esp_ota_get_next_update_partition(NULL);
    if (!update_part) {
        ESP_LOGE(TAG, "No OTA partition found");
        report_result(false, "No OTA partition");
        goto cleanup;
    }

    esp_http_client_config_t http_cfg = {
        .url = params->url,
        .timeout_ms = 30000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&http_cfg);
    if (!client) {
        report_result(false, "HTTP client init failed");
        goto cleanup;
    }

    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "HTTP open failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        report_result(false, "Cannot connect to server");
        goto cleanup;
    }

    int content_len = esp_http_client_fetch_headers(client);
    if (content_len <= 0) {
        ESP_LOGE(TAG, "Invalid content length: %d", content_len);
        esp_http_client_cleanup(client);
        report_result(false, "Invalid firmware size");
        goto cleanup;
    }

    if (content_len > update_part->size) {
        ESP_LOGE(TAG, "Firmware too large: %d > %lu", content_len, update_part->size);
        esp_http_client_cleanup(client);
        report_result(false, "Firmware exceeds partition size");
        goto cleanup;
    }

    esp_ota_handle_t ota_handle = 0;
    err = esp_ota_begin(update_part, OTA_WITH_SEQUENTIAL_WRITES, &ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA begin failed: %s", esp_err_to_name(err));
        esp_http_client_cleanup(client);
        report_result(false, "OTA init failed");
        goto cleanup;
    }

    uint8_t buf[1024];
    int total_read = 0;
    int last_pct = -1;

    while (total_read < content_len) {
        int to_read = sizeof(buf);
        if (total_read + to_read > content_len) to_read = content_len - total_read;

        int read = esp_http_client_read(client, (char *)buf, to_read);
        if (read <= 0) {
            ESP_LOGE(TAG, "Read error at %d/%d", total_read, content_len);
            esp_ota_abort(ota_handle);
            esp_http_client_cleanup(client);
            report_result(false, "Download failed");
            goto cleanup;
        }

        err = esp_ota_write(ota_handle, buf, read);
        if (err != ESP_OK) {
            ESP_LOGE(TAG, "Write error: %s", esp_err_to_name(err));
            esp_ota_abort(ota_handle);
            esp_http_client_cleanup(client);
            report_result(false, "Flash write failed");
            goto cleanup;
        }

        total_read += read;
        int pct = (total_read * 100) / content_len;
        ota_progress = pct;
        if (pct != last_pct) {
            report_progress(pct);
            last_pct = pct;
        }
    }

    esp_http_client_cleanup(client);

    err = esp_ota_end(ota_handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "OTA end failed: %s", esp_err_to_name(err));
        report_result(false, "OTA verify failed");
        goto cleanup;
    }

    ESP_LOGI(TAG, "OTA success, restarting...");
    report_progress(100);
    ota_status = 2;
    report_result(true, NULL);
    free(params);
    ota_in_progress = false;
    vTaskDelay(pdMS_TO_TICKS(2000));
    esp_restart();

cleanup:
    ota_in_progress = false;
    if (ota_status != 2) ota_status = 3;
    status_led_set_state(STATUS_LED_READY);
    free(params);
    vTaskDelete(NULL);
}
