/**
 * @file    device_id.c
 * @brief   设备ID和MQTT主题生成实现 (ESP8266版本)
 */

#include <stdio.h>
#include <string.h>
#include "esp_wifi.h"
#include "esp_system.h"
#include "esp_log.h"
#include "device_id.h"

static const char *TAG = "device_id";

/* 全局变量定义 */
char base_device_id[DEVICE_ID_LEN] = {0};
char base_device_name[DEVICE_NAME_LEN] = {0};
char device_avail_topic[TOPIC_LEN] = {0};
char rssi_config_topic[TOPIC_LEN] = {0};
char rssi_state_topic[TOPIC_LEN] = {0};
char device_status_topic[TOPIC_LEN] = {0};
char device_command_topic[TOPIC_LEN] = {0};
char device_response_topic[TOPIC_LEN] = {0};

void device_id_init(void)
{
    uint8_t mac[6];
    esp_wifi_get_mac(ESP_IF_WIFI_STA, mac);

    /* 生成12字符小写MAC字符串 */
    snprintf(base_device_id, sizeof(base_device_id),
             "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    /* 设备名称 */
    snprintf(base_device_name, sizeof(base_device_name),
             "ESP8266 Switch (%s)", base_device_id);

    /* 共享可用性主题 */
    snprintf(device_avail_topic, sizeof(device_avail_topic),
             "homeassistant/switch/%s/availability", base_device_id);

    /* RSSI传感器主题 */
    snprintf(rssi_config_topic, sizeof(rssi_config_topic),
             "homeassistant/sensor/%s/rssi/config", base_device_id);
    snprintf(rssi_state_topic, sizeof(rssi_state_topic),
             "homeassistant/sensor/%s/rssi/state", base_device_id);

    /* 设备状态主题 */
    snprintf(device_status_topic, sizeof(device_status_topic),
             "device/%s/status", base_device_id);
    snprintf(device_command_topic, sizeof(device_command_topic),
             "device/%s/command", base_device_id);
    snprintf(device_response_topic, sizeof(device_response_topic),
             "device/%s/response", base_device_id);

    ESP_LOGI(TAG, "Device ID: %s", base_device_id);
    ESP_LOGI(TAG, "Device Name: %s", base_device_name);
}
