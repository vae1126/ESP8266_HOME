/**
 * @file    rssi_reporter.c
 * @brief   RSSI信号强度上报实现 (ESP8266版本)
 */

#include <stdio.h>
#include <stdlib.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_wifi.h"
#include "esp_log.h"
#include "device_id.h"
#include "mqtt_ha.h"

static const char *TAG = "rssi_reporter";

/**
 * @brief  RSSI上报任务
 */
static void rssi_reporter_task(void *pvParameters)
{
    ESP_LOGI(TAG, "RSSI reporter task started");

    int cycle = 0;
    int last_rssi = 0;

    while (1) {
        /* 获取RSSI */
        wifi_ap_record_t ap_info;
        if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
            int rssi = ap_info.rssi;

            /* 只在变化超过2dBm时发布 */
            if (abs(rssi - last_rssi) >= 2 || cycle == 0) {
                char rssi_str[16];
                snprintf(rssi_str, sizeof(rssi_str), "%d", rssi);
                mqtt_ha_publish(rssi_state_topic, rssi_str, 1);
                last_rssi = rssi;
                ESP_LOGD(TAG, "RSSI: %d dBm", rssi);
            }
        }

        /* 每60秒上报设备状态 */
        if (cycle % 2 == 0) {
            mqtt_ha_report_device_status();
        }

        /* 每5分钟刷新发现配置 */
        if (cycle % 10 == 0) {
            mqtt_ha_publish_discovery();
        }

        cycle++;
        vTaskDelay(pdMS_TO_TICKS(30000));  /* 30秒间隔 */
    }
}

void rssi_reporter_start(void)
{
    ESP_LOGI(TAG, "Starting RSSI reporter");
    xTaskCreate(rssi_reporter_task, "rssi_task", 4096, NULL, 5, NULL);
}
