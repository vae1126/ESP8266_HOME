#include "mqtt_ha.h"
#include "mqtt_bridge.h"
#include "device_id.h"
#include "switch_control.h"
#include "status_led.h"
#include "device_config.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_timer.h"
#include "tcpip_adapter.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include <string.h>
#include <stdio.h>

static const char *TAG = "mqtt_ha";

static TimerHandle_t s_status_timer = NULL;

static void status_timer_cb(TimerHandle_t timer)
{
    mqtt_ha_report_device_status();
}

static int switch_cmd_topic_lens[8] = {0};
static int device_cmd_topic_len = 0;

#define publish(t, p, r) mqtt_bridge_publish(t, p, r)
#define subscribe(t, q)  mqtt_bridge_subscribe(t, q)

static void handle_device_command(const char *data, int data_len);

static void init_topics(void)
{
#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    for (int i = 0; i < CONFIG_SWITCH_COUNT; i++) {
        switch_cmd_topic_lens[i] = strlen(switch_command_topics[i]);
    }
#endif
    device_cmd_topic_len = strlen(device_command_topic);
}

static void publish_switch_state(const char *topic, bool on)
{
    publish(topic, on ? "ON" : "OFF", 1);
}

static bool parse_switch_command(const char *data, int data_len, bool *on)
{
    if (data_len >= 2 && strncmp(data, "ON", 2) == 0) {
        if (*on != true) { *on = true; return true; }
    } else if (data_len >= 3 && strncmp(data, "OFF", 3) == 0) {
        if (*on != false) { *on = false; return true; }
    }
    return false;
}

static void handle_switch_command(int index, const char *data, int data_len)
{
    if (index < 0 || index >= get_switch_count()) return;
    bool on = led_switch_get_state(index);
    if (parse_switch_command(data, data_len, &on)) {
        led_switch_set_state(index, on);
        publish_switch_state(switch_state_topics[index], on);
    }
}

static void publish_all_states(void)
{
#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    for (int i = 0; i < CONFIG_SWITCH_COUNT; i++) {
        publish_switch_state(switch_state_topics[i], led_switch_get_state(i));
    }
#endif
}

static void publish_switch_discovery(int index)
{
    cJSON *cfg = cJSON_CreateObject();
    if (!cfg) return;

    char name[32], uid[32];
    snprintf(name, sizeof(name), "Switch %d", index + 1);
    snprintf(uid, sizeof(uid), "%s_switch_%d", base_device_id, index);

    cJSON_AddStringToObject(cfg, "name", name);
    cJSON_AddStringToObject(cfg, "unique_id", uid);
    cJSON_AddStringToObject(cfg, "cmd_t", switch_command_topics[index]);
    cJSON_AddStringToObject(cfg, "stat_t", switch_state_topics[index]);
    cJSON_AddStringToObject(cfg, "avty_t", device_avail_topic);

    cJSON *dev = cJSON_CreateObject();
    cJSON_AddStringToObject(dev, "identifiers", base_device_id);
    cJSON_AddStringToObject(dev, "name", base_device_name);
    cJSON_AddStringToObject(dev, "manufacturer", "Espressif");
    cJSON_AddItemToObject(cfg, "device", dev);

    char *cfg_str = cJSON_PrintUnformatted(cfg);
    if (cfg_str) { publish(switch_config_topics[index], cfg_str, 1); cJSON_free(cfg_str); }
    cJSON_Delete(cfg);
}

static void publish_all_discovery_configs(void)
{
#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    for (int i = 0; i < CONFIG_SWITCH_COUNT; i++) {
        publish_switch_discovery(i);
    }
#endif
}

static void on_connected(void)
{
    ESP_LOGI(TAG, "MQTT connected");
    status_led_set_state(STATUS_LED_READY);
    publish(device_avail_topic, "online", 1);

#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    for (int i = 0; i < CONFIG_SWITCH_COUNT; i++) {
        subscribe(switch_command_topics[i], 1);
    }
#endif
    subscribe(device_command_topic, 1);

    mqtt_ha_report_device_status();
    publish_all_discovery_configs();
    publish_all_states();
}

static bool on_data(const char *topic, int topic_len, const char *data, int data_len)
{
#if defined(CONFIG_DEVICE_TYPE_SWITCH)
    for (int i = 0; i < CONFIG_SWITCH_COUNT; i++) {
        if (topic_len == switch_cmd_topic_lens[i] &&
            strncmp(topic, switch_command_topics[i], topic_len) == 0) {
            handle_switch_command(i, data, data_len);
            return true;
        }
    }
#endif
    if (topic_len == device_cmd_topic_len &&
        strncmp(topic, device_command_topic, topic_len) == 0) {
        handle_device_command(data, data_len);
        return true;
    }
    return false;
}

static void parse_device_target(cJSON *params_json, char *type_str, size_t type_size, int *index)
{
    type_str[0] = '\0';
    *index = 0;
    if (!params_json) return;
    cJSON *type_json = cJSON_GetObjectItem(params_json, "type");
    if (type_json && cJSON_IsString(type_json)) {
        strncpy(type_str, type_json->valuestring, type_size - 1);
    }
    cJSON *index_json = cJSON_GetObjectItem(params_json, "index");
    if (index_json && cJSON_IsNumber(index_json)) {
        *index = index_json->valueint;
        if (*index < 0) *index = 0;
    }
}

static void handle_device_command(const char *data, int data_len)
{
    cJSON *root = cJSON_Parse(data);
    if (!root) { ESP_LOGE(TAG, "Failed to parse device command JSON"); return; }
    cJSON *action_json = cJSON_GetObjectItem(root, "action");
    cJSON *params_json = cJSON_GetObjectItem(root, "params");
    cJSON *request_id_json = cJSON_GetObjectItem(root, "request_id");
    if (!action_json || !cJSON_IsString(action_json)) {
        ESP_LOGE(TAG, "Missing or invalid 'action' field");
        cJSON_Delete(root);
        return;
    }
    const char *action = action_json->valuestring;
    const char *request_id = (request_id_json && cJSON_IsString(request_id_json)) ? request_id_json->valuestring : "";
    bool success = true;
    char device_type[16] = {0};
    int device_index = 0;
    parse_device_target(params_json, device_type, sizeof(device_type), &device_index);

    if (strcmp(action, "turn_on") == 0) {
        led_switch_set_state(device_index, true);
    } else if (strcmp(action, "turn_off") == 0) {
        led_switch_set_state(device_index, false);
    } else if (strcmp(action, "reboot") == 0) {
        cJSON *resp = cJSON_CreateObject();
        if (resp) {
            cJSON_AddStringToObject(resp, "request_id", request_id);
            cJSON_AddBoolToObject(resp, "success", true);
            char *rs = cJSON_PrintUnformatted(resp);
            if (rs) { publish(device_response_topic, rs, 0); cJSON_free(rs); }
            cJSON_Delete(resp);
        }
        cJSON_Delete(root);
        vTaskDelay(pdMS_TO_TICKS(500));
        esp_restart();
    } else if (strcmp(action, "set_mqtt_broker") == 0) {
        if (!params_json) { success = false; }
        else {
            cJSON *broker_json = cJSON_GetObjectItem(params_json, "broker");
            if (broker_json && cJSON_IsString(broker_json)) {
                const char *broker = broker_json->valuestring;
                if (strncmp(broker, "mqtt://", 7) != 0 && strncmp(broker, "mqtts://", 8) != 0) { success = false; }
                else {
                    if (device_id_set_mqtt_broker(broker) == ESP_OK) {
                        cJSON *resp = cJSON_CreateObject();
                        if (resp) {
                            cJSON_AddStringToObject(resp, "request_id", request_id);
                            cJSON_AddBoolToObject(resp, "success", true);
                            char *rs = cJSON_PrintUnformatted(resp);
                            if (rs) { publish(device_response_topic, rs, 0); cJSON_free(rs); }
                            cJSON_Delete(resp);
                        }
                        cJSON_Delete(root);
                        vTaskDelay(pdMS_TO_TICKS(500));
                        esp_restart();
                        return;
                    } else { success = false; }
                }
            } else { success = false; }
        }
    } else { success = false; }

    cJSON *response = cJSON_CreateObject();
    if (response) {
        cJSON_AddStringToObject(response, "request_id", request_id);
        cJSON_AddBoolToObject(response, "success", success);
        char *rs = cJSON_PrintUnformatted(response);
        if (rs) { publish(device_response_topic, rs, 0); cJSON_free(rs); }
        cJSON_Delete(response);
    }
    cJSON_Delete(root);
}

void mqtt_ha_init(void)
{
    init_topics();

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char client_id[32];
    snprintf(client_id, sizeof(client_id), "ESP8266_%02X%02X%02X%02X%02X%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    char broker_url[MQTT_BROKER_URL_LEN];
    device_id_get_mqtt_broker(broker_url, sizeof(broker_url));

    mqtt_bridge_set_connected_cb(on_connected);
    mqtt_bridge_add_data_cb(on_data);

    s_status_timer = xTimerCreate("status", pdMS_TO_TICKS(60000), pdTRUE, NULL, status_timer_cb);
    if (s_status_timer) xTimerStart(s_status_timer, 0);

    mqtt_bridge_init(broker_url, client_id, device_avail_topic, "offline");
}

void mqtt_ha_report_device_status(void)
{
    char ip_str[16] = {0};
    tcpip_adapter_ip_info_t ip_info;
    if (tcpip_adapter_get_ip_info(TCPIP_ADAPTER_IF_STA, &ip_info) == ESP_OK) {
        snprintf(ip_str, sizeof(ip_str), IPSTR, IP2STR(&ip_info.ip));
    }

    uint8_t mac[6];
    esp_read_mac(mac, ESP_MAC_WIFI_STA);
    char mac_str[18];
    snprintf(mac_str, sizeof(mac_str), "%02X:%02X:%02X:%02X:%02X:%02X",
             mac[0], mac[1], mac[2], mac[3], mac[4], mac[5]);

    wifi_ap_record_t ap_info;
    int rssi = -100;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        rssi = ap_info.rssi;
    }

    char payload[512];
    int uptime = esp_timer_get_time() / 1000000;
    int free_h = esp_get_free_heap_size();
    snprintf(payload, sizeof(payload),
        "{\"name\":\"%s\",\"mac\":\"%s\",\"ip\":\"%s\",\"version\":\"%s\","
        "\"hardware\":\"%s\",\"device_type\":\"relay\",\"rssi\":%d,"
        "\"uptime\":%d,\"free_heap\":%d}",
        base_device_name, mac_str, ip_str, FIRMWARE_VERSION,
        CONFIG_IDF_TARGET, rssi, uptime, free_h);
    publish(device_status_topic, payload, 0);
}

void mqtt_ha_publish_discovery(void)
{
    publish_all_discovery_configs();
}

void mqtt_ha_publish_states(void)
{
    publish_all_states();
}
