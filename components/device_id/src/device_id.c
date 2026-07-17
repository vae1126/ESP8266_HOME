#include "device_id.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "sdkconfig.h"
#include <stdio.h>
#include <string.h>

static const char *TAG = "device_id";

#define MQTT_NVS_NAMESPACE "mqtt_config"
#define MQTT_NVS_KEY_BROKER "broker_url"

char base_device_id[DEVICE_ID_LEN] = {0};
char base_device_name[DEVICE_NAME_LEN] = {0};
char device_avail_topic[TOPIC_LEN] = {0};

char device_status_topic[TOPIC_LEN] = {0};
char device_command_topic[TOPIC_LEN] = {0};
char device_response_topic[TOPIC_LEN] = {0};

#if defined(CONFIG_DEVICE_TYPE_SWITCH)
char switch_config_topics[CONFIG_SWITCH_COUNT][TOPIC_LEN] = {{0}};
char switch_command_topics[CONFIG_SWITCH_COUNT][TOPIC_LEN] = {{0}};
char switch_state_topics[CONFIG_SWITCH_COUNT][TOPIC_LEN] = {{0}};
#endif

void device_id_init(void)
{
    uint8_t mac[6];
    esp_err_t err = esp_read_mac(mac, ESP_MAC_WIFI_STA);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to read MAC address: %s", esp_err_to_name(err));
        memset(mac, 0, sizeof(mac));
    }

    char mac_str[13];
    snprintf(mac_str, sizeof(mac_str), "%02x%02x%02x%02x%02x%02x",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    snprintf(base_device_id, sizeof(base_device_id), "%s", mac_str);

    snprintf(base_device_name, sizeof(base_device_name), "ESP8266-SWITCH-%s", mac_str);

    for (int i = 0; base_device_name[i]; i++) {
        if (base_device_name[i] >= 'a' && base_device_name[i] <= 'z') {
            base_device_name[i] -= 32;
        }
    }

    snprintf(device_avail_topic, sizeof(device_avail_topic),
             "homeassistant/device/%s/availability", base_device_id);

    snprintf(device_status_topic, sizeof(device_status_topic),
             "device/%s/status", base_device_id);
    snprintf(device_command_topic, sizeof(device_command_topic),
             "device/%s/command", base_device_id);
    snprintf(device_response_topic, sizeof(device_response_topic),
             "device/%s/response", base_device_id);

#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    for (int i = 0; i < CONFIG_SWITCH_COUNT; i++) {
        snprintf(switch_config_topics[i], TOPIC_LEN,
                 "homeassistant/switch/%s_switch_%d/config", base_device_id, i);
        snprintf(switch_command_topics[i], TOPIC_LEN,
                 "homeassistant/switch/%s_switch_%d/set", base_device_id, i);
        snprintf(switch_state_topics[i], TOPIC_LEN,
                 "homeassistant/switch/%s_switch_%d/state", base_device_id, i);
    }
#endif

    ESP_LOGI(TAG, "Device ID: %s", base_device_id);
    ESP_LOGI(TAG, "Device Status Topic: %s", device_status_topic);
}

char* device_id_get_mqtt_broker(char *url, size_t size)
{
    nvs_handle_t h;
    if (nvs_open(MQTT_NVS_NAMESPACE, NVS_READONLY, &h) == ESP_OK) {
        size_t len = size;
        if (nvs_get_str(h, MQTT_NVS_KEY_BROKER, url, &len) == ESP_OK && len > 1) {
            nvs_close(h);
            ESP_LOGI(TAG, "MQTT broker from NVS: %s", url);
            return url;
        }
        nvs_close(h);
    }
    strncpy(url, CONFIG_MQTT_BROKER_URL, size - 1);
    url[size - 1] = '\0';
    ESP_LOGI(TAG, "MQTT broker default: %s", url);
    return url;
}

esp_err_t device_id_set_mqtt_broker(const char *url)
{
    if (!url || strlen(url) == 0 || strlen(url) >= MQTT_BROKER_URL_LEN) {
        return ESP_ERR_INVALID_ARG;
    }
    if (strncmp(url, "mqtt://", 7) != 0 && strncmp(url, "mqtts://", 8) != 0) {
        ESP_LOGE(TAG, "Invalid MQTT URL format: %s", url);
        return ESP_ERR_INVALID_ARG;
    }

    nvs_handle_t h;
    esp_err_t err = nvs_open(MQTT_NVS_NAMESPACE, NVS_READWRITE, &h);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return err;
    }
    err = nvs_set_str(h, MQTT_NVS_KEY_BROKER, url);
    if (err == ESP_OK) {
        err = nvs_commit(h);
    }
    nvs_close(h);

    if (err == ESP_OK) {
        ESP_LOGI(TAG, "MQTT broker saved to NVS: %s", url);
    } else {
        ESP_LOGE(TAG, "NVS save failed: %s", esp_err_to_name(err));
    }
    return err;
}
