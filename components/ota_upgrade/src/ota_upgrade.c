/**
 * @file    ota_upgrade.c
 * @brief   OTA远程升级实现 (ESP8266版本)
 */

#include <string.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "esp_https_ota.h"
#include "mqtt_client.h"
#include "cJSON.h"
#include "device_id.h"
#include "mqtt_ha.h"
#include "ota_upgrade.h"
#include "version.h"

static const char *TAG = "ota_upgrade";

/* MQTT主题 */
static char ota_notify_topic[TOPIC_LEN] = {0};
static char ota_progress_topic[TOPIC_LEN] = {0};
static char ota_result_topic[TOPIC_LEN] = {0};

/* OTA状态 */
static volatile int ota_status = 0;
static volatile bool ota_in_progress = false;
static volatile int ota_progress = 0;

static void report_progress(int progress)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddNumberToObject(root, "progress", progress);
    cJSON_AddStringToObject(root, "device_id", base_device_id);

    char *str = cJSON_PrintUnformatted(root);
    if (str) {
        mqtt_ha_publish(ota_progress_topic, str, 0);
        free(str);
    }
    cJSON_Delete(root);
}

static void report_result(bool success, const char *error_msg)
{
    cJSON *root = cJSON_CreateObject();
    cJSON_AddBoolToObject(root, "success", success);
    cJSON_AddStringToObject(root, "device_id", base_device_id);
    cJSON_AddStringToObject(root, "firmware_version", FIRMWARE_VERSION);
    if (error_msg) {
        cJSON_AddStringToObject(root, "error", error_msg);
    }

    char *str = cJSON_PrintUnformatted(root);
    if (str) {
        mqtt_ha_publish(ota_result_topic, str, 0);
        free(str);
    }
    cJSON_Delete(root);
}

static void ota_task(void *pvParameters)
{
    char *url = (char *)pvParameters;

    ESP_LOGI(TAG, "Starting OTA: %s", url);

    /* ESP8266的esp_https_ota直接接收http_client_config */
    esp_http_client_config_t config = {
        .url = url,
        .timeout_ms = 30000,
    };

    esp_err_t ret = esp_https_ota(&config);

    if (ret == ESP_OK) {
        ESP_LOGI(TAG, "OTA successful");
        ota_status = 2;
        ota_progress = 100;
        report_progress(100);
        report_result(true, NULL);

        vTaskDelay(pdMS_TO_TICKS(2000));
        esp_restart();
    } else {
        ESP_LOGE(TAG, "OTA failed: %s", esp_err_to_name(ret));
        ota_status = 3;
        ota_progress = 0;
        report_result(false, esp_err_to_name(ret));
    }

    ota_in_progress = false;
    free(url);
    vTaskDelete(NULL);
}

static void handle_ota_command(const char *data, int data_len)
{
    char *json_str = malloc(data_len + 1);
    if (!json_str) return;
    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0';
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (!root) {
        ESP_LOGE(TAG, "Failed to parse OTA command");
        return;
    }

    cJSON *url_json = cJSON_GetObjectItem(root, "url");
    if (!url_json || !cJSON_IsString(url_json)) {
        ESP_LOGE(TAG, "Missing URL in OTA command");
        cJSON_Delete(root);
        return;
    }

    if (ota_in_progress) {
        ESP_LOGW(TAG, "OTA already in progress");
        report_result(false, "OTA already in progress");
        cJSON_Delete(root);
        return;
    }

    char *url = strdup(url_json->valuestring);
    cJSON_Delete(root);

    if (!url) {
        ESP_LOGE(TAG, "Failed to allocate URL");
        return;
    }

    ota_in_progress = true;
    ota_status = 1;
    ota_progress = 0;

    xTaskCreate(ota_task, "ota_task", 8192, url, 5, NULL);
}

esp_err_t ota_upgrade_init(void)
{
    ESP_LOGI(TAG, "OTA upgrade init");

    snprintf(ota_notify_topic, sizeof(ota_notify_topic),
             "device/%s/ota/notify", base_device_id);
    snprintf(ota_progress_topic, sizeof(ota_progress_topic),
             "device/%s/ota/progress", base_device_id);
    snprintf(ota_result_topic, sizeof(ota_result_topic),
             "device/%s/ota/result", base_device_id);

    ESP_LOGI(TAG, "OTA Notify Topic: %s", ota_notify_topic);

    return ESP_OK;
}

void ota_upgrade_subscribe(void)
{
    esp_mqtt_client_handle_t client = mqtt_ha_get_client();
    if (client) {
        esp_mqtt_client_subscribe(client, ota_notify_topic, 1);
        ESP_LOGI(TAG, "Subscribed to OTA topic");
    }
}

int ota_upgrade_get_status(void)
{
    return ota_status;
}

int ota_upgrade_get_progress(void)
{
    return ota_progress;
}
