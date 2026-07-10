/**
 * @file    mqtt_ha.c
 * @brief   MQTT + Home Assistant集成实现 (ESP8266版本)
 *
 * 支持：PWM RGB灯、单色灯、WS2812灯带、开关
 */

#include <stdio.h>
#include <string.h>
#include <stdlib.h>
#include "mqtt_client.h"
#include "cJSON.h"
#include "esp_log.h"
#include "esp_wifi.h"
#include "esp_system.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "device_id.h"
#include "led_control.h"
#include "device_config.h"
#include "mqtt_ha.h"
#include "ota_upgrade.h"
#include "version.h"

static const char *TAG = "mqtt_ha";

/* MQTT客户端句柄 */
static esp_mqtt_client_handle_t client = NULL;

/* 主题缓冲区 */
#define MAX_ENTITIES 8
#define TOPIC_LEN 128

/* PWM RGB主题 */
static char rgb_config_topic[MAX_ENTITIES][TOPIC_LEN] = {0};
static char rgb_command_topic[MAX_ENTITIES][TOPIC_LEN] = {0};
static char rgb_state_topic[MAX_ENTITIES][TOPIC_LEN] = {0};

/* 单色灯主题 */
static char single_config_topic[MAX_ENTITIES][TOPIC_LEN] = {0};
static char single_command_topic[MAX_ENTITIES][TOPIC_LEN] = {0};
static char single_state_topic[MAX_ENTITIES][TOPIC_LEN] = {0};

/* WS2812主题 */
static char ws2812_config_topic[MAX_ENTITIES][TOPIC_LEN] = {0};
static char ws2812_command_topic[MAX_ENTITIES][TOPIC_LEN] = {0};
static char ws2812_state_topic[MAX_ENTITIES][TOPIC_LEN] = {0};

/* 开关主题 */
static char switch_config_topic[MAX_ENTITIES][TOPIC_LEN] = {0};
static char switch_command_topic[MAX_ENTITIES][TOPIC_LEN] = {0};
static char switch_state_topic[MAX_ENTITIES][TOPIC_LEN] = {0};

/* ==========================================================================
 * 主题生成
 * ========================================================================== */

static void generate_topics(void)
{
    device_caps_t caps = get_device_capabilities();

    /* PWM RGB主题 */
    if (caps.has_pwm_rgb) {
        for (int i = 0; i < caps.pwm_rgb_count && i < MAX_ENTITIES; i++) {
            snprintf(rgb_config_topic[i], TOPIC_LEN, "homeassistant/light/%s_rgb_%d/config", base_device_id, i);
            snprintf(rgb_command_topic[i], TOPIC_LEN, "homeassistant/light/%s_rgb_%d/set", base_device_id, i);
            snprintf(rgb_state_topic[i], TOPIC_LEN, "homeassistant/light/%s_rgb_%d/state", base_device_id, i);
        }
    }

    /* 单色灯主题 */
    if (caps.has_pwm_single) {
        for (int i = 0; i < caps.pwm_single_count && i < MAX_ENTITIES; i++) {
            snprintf(single_config_topic[i], TOPIC_LEN, "homeassistant/light/%s_single_%d/config", base_device_id, i);
            snprintf(single_command_topic[i], TOPIC_LEN, "homeassistant/light/%s_single_%d/set", base_device_id, i);
            snprintf(single_state_topic[i], TOPIC_LEN, "homeassistant/light/%s_single_%d/state", base_device_id, i);
        }
    }

    /* WS2812主题 */
    if (caps.has_ws2812) {
        for (int i = 0; i < caps.ws2812_count && i < MAX_ENTITIES; i++) {
            snprintf(ws2812_config_topic[i], TOPIC_LEN, "homeassistant/light/%s_ws2812_%d/config", base_device_id, i);
            snprintf(ws2812_command_topic[i], TOPIC_LEN, "homeassistant/light/%s_ws2812_%d/set", base_device_id, i);
            snprintf(ws2812_state_topic[i], TOPIC_LEN, "homeassistant/light/%s_ws2812_%d/state", base_device_id, i);
        }
    }

    /* 开关主题 */
    if (caps.has_switch) {
        for (int i = 0; i < caps.switch_count && i < MAX_ENTITIES; i++) {
            snprintf(switch_config_topic[i], TOPIC_LEN, "homeassistant/switch/%s_switch_%d/config", base_device_id, i);
            snprintf(switch_command_topic[i], TOPIC_LEN, "homeassistant/switch/%s_switch_%d/set", base_device_id, i);
            snprintf(switch_state_topic[i], TOPIC_LEN, "homeassistant/switch/%s_switch_%d/state", base_device_id, i);
        }
    }
}

/* ==========================================================================
 * 发现配置发布
 * ========================================================================== */

static cJSON* create_device_json(void)
{
    cJSON *dev = cJSON_CreateObject();
    cJSON_AddStringToObject(dev, "identifiers", base_device_id);
    cJSON_AddStringToObject(dev, "name", base_device_name);
    cJSON_AddStringToObject(dev, "model", "ESP8266 Smart Home");
    cJSON_AddStringToObject(dev, "manufacturer", "Espressif");
    return dev;
}

static void publish_rgb_discovery(int index)
{
    cJSON *cfg = cJSON_CreateObject();
    if (!cfg) return;

    char name[32];
    snprintf(name, sizeof(name), "RGB Light %d", index + 1);
    cJSON_AddStringToObject(cfg, "name", name);

    char uid[32];
    snprintf(uid, sizeof(uid), "%s_rgb_%d", base_device_id, index);
    cJSON_AddStringToObject(cfg, "unique_id", uid);

    cJSON_AddStringToObject(cfg, "cmd_t", rgb_command_topic[index]);
    cJSON_AddStringToObject(cfg, "stat_t", rgb_state_topic[index]);
    cJSON_AddStringToObject(cfg, "schema", "json");
    cJSON_AddBoolToObject(cfg, "brightness", true);
    cJSON_AddBoolToObject(cfg, "rgb", true);
    cJSON_AddStringToObject(cfg, "avty_t", device_avail_topic);
    cJSON_AddStringToObject(cfg, "pl_avail", "online");
    cJSON_AddStringToObject(cfg, "pl_not_avail", "offline");
    cJSON_AddItemToObject(cfg, "device", create_device_json());

    char *cfg_str = cJSON_PrintUnformatted(cfg);
    if (cfg_str) {
        mqtt_ha_publish(rgb_config_topic[index], cfg_str, 1);
        free(cfg_str);
    }
    cJSON_Delete(cfg);
}

static void publish_single_discovery(int index)
{
    cJSON *cfg = cJSON_CreateObject();
    if (!cfg) return;

    char name[32];
    snprintf(name, sizeof(name), "Light %d", index + 1);
    cJSON_AddStringToObject(cfg, "name", name);

    char uid[32];
    snprintf(uid, sizeof(uid), "%s_single_%d", base_device_id, index);
    cJSON_AddStringToObject(cfg, "unique_id", uid);

    cJSON_AddStringToObject(cfg, "cmd_t", single_command_topic[index]);
    cJSON_AddStringToObject(cfg, "stat_t", single_state_topic[index]);
    cJSON_AddStringToObject(cfg, "schema", "json");
    cJSON_AddBoolToObject(cfg, "brightness", true);
    cJSON_AddStringToObject(cfg, "avty_t", device_avail_topic);
    cJSON_AddStringToObject(cfg, "pl_avail", "online");
    cJSON_AddStringToObject(cfg, "pl_not_avail", "offline");
    cJSON_AddItemToObject(cfg, "device", create_device_json());

    char *cfg_str = cJSON_PrintUnformatted(cfg);
    if (cfg_str) {
        mqtt_ha_publish(single_config_topic[index], cfg_str, 1);
        free(cfg_str);
    }
    cJSON_Delete(cfg);
}

static void publish_ws2812_discovery(int index)
{
    cJSON *cfg = cJSON_CreateObject();
    if (!cfg) return;

    char name[32];
    snprintf(name, sizeof(name), "WS2812 %d", index + 1);
    cJSON_AddStringToObject(cfg, "name", name);

    char uid[32];
    snprintf(uid, sizeof(uid), "%s_ws2812_%d", base_device_id, index);
    cJSON_AddStringToObject(cfg, "unique_id", uid);

    cJSON_AddStringToObject(cfg, "cmd_t", ws2812_command_topic[index]);
    cJSON_AddStringToObject(cfg, "stat_t", ws2812_state_topic[index]);
    cJSON_AddStringToObject(cfg, "schema", "json");
    cJSON_AddBoolToObject(cfg, "brightness", true);
    cJSON_AddBoolToObject(cfg, "rgb", true);
    cJSON_AddStringToObject(cfg, "avty_t", device_avail_topic);
    cJSON_AddStringToObject(cfg, "pl_avail", "online");
    cJSON_AddStringToObject(cfg, "pl_not_avail", "offline");
    cJSON_AddItemToObject(cfg, "device", create_device_json());

    char *cfg_str = cJSON_PrintUnformatted(cfg);
    if (cfg_str) {
        mqtt_ha_publish(ws2812_config_topic[index], cfg_str, 1);
        free(cfg_str);
    }
    cJSON_Delete(cfg);
}

static void publish_switch_discovery(int index)
{
    cJSON *cfg = cJSON_CreateObject();
    if (!cfg) return;

    char name[32];
    snprintf(name, sizeof(name), "Switch %d", index + 1);
    cJSON_AddStringToObject(cfg, "name", name);

    char uid[32];
    snprintf(uid, sizeof(uid), "%s_switch_%d", base_device_id, index);
    cJSON_AddStringToObject(cfg, "unique_id", uid);

    cJSON_AddStringToObject(cfg, "cmd_t", switch_command_topic[index]);
    cJSON_AddStringToObject(cfg, "stat_t", switch_state_topic[index]);
    cJSON_AddStringToObject(cfg, "payload_on", "ON");
    cJSON_AddStringToObject(cfg, "payload_off", "OFF");
    cJSON_AddStringToObject(cfg, "state_on", "ON");
    cJSON_AddStringToObject(cfg, "state_off", "OFF");
    cJSON_AddBoolToObject(cfg, "optimistic", false);
    cJSON_AddStringToObject(cfg, "avty_t", device_avail_topic);
    cJSON_AddStringToObject(cfg, "pl_avail", "online");
    cJSON_AddStringToObject(cfg, "pl_not_avail", "offline");
    cJSON_AddItemToObject(cfg, "device", create_device_json());

    char *cfg_str = cJSON_PrintUnformatted(cfg);
    if (cfg_str) {
        mqtt_ha_publish(switch_config_topic[index], cfg_str, 1);
        free(cfg_str);
    }
    cJSON_Delete(cfg);
}

static void publish_rssi_discovery(void)
{
    cJSON *cfg = cJSON_CreateObject();
    if (!cfg) return;

    cJSON_AddStringToObject(cfg, "name", "Wi-Fi RSSI");
    cJSON_AddStringToObject(cfg, "state_topic", rssi_state_topic);
    cJSON_AddStringToObject(cfg, "unit_of_measurement", "dBm");
    cJSON_AddStringToObject(cfg, "device_class", "signal_strength");
    cJSON_AddStringToObject(cfg, "state_class", "measurement");
    cJSON_AddStringToObject(cfg, "entity_category", "diagnostic");

    char uid[32];
    snprintf(uid, sizeof(uid), "wifi_rssi_%s", base_device_id);
    cJSON_AddStringToObject(cfg, "unique_id", uid);
    cJSON_AddItemToObject(cfg, "device", create_device_json());

    char *cfg_str = cJSON_PrintUnformatted(cfg);
    if (cfg_str) {
        mqtt_ha_publish(rssi_config_topic, cfg_str, 1);
        free(cfg_str);
    }
    cJSON_Delete(cfg);
}

void mqtt_ha_publish_discovery(void)
{
    device_caps_t caps = get_device_capabilities();

    if (caps.has_pwm_rgb) {
        for (int i = 0; i < caps.pwm_rgb_count && i < MAX_ENTITIES; i++) {
            publish_rgb_discovery(i);
        }
    }
    if (caps.has_pwm_single) {
        for (int i = 0; i < caps.pwm_single_count && i < MAX_ENTITIES; i++) {
            publish_single_discovery(i);
        }
    }
    if (caps.has_ws2812) {
        for (int i = 0; i < caps.ws2812_count && i < MAX_ENTITIES; i++) {
            publish_ws2812_discovery(i);
        }
    }
    if (caps.has_switch) {
        for (int i = 0; i < caps.switch_count && i < MAX_ENTITIES; i++) {
            publish_switch_discovery(i);
        }
    }
    publish_rssi_discovery();
    ESP_LOGI(TAG, "All discovery configs published");
}

/* ==========================================================================
 * 状态发布
 * ========================================================================== */

/**
 * @brief  发布灯光状态到MQTT
 *
 * HA MQTT JSON schema要求：
 *   state: "ON" / "OFF" (必须)
 *   brightness: 0-255 (仅在ON时发送)
 *   color: {r, g, b} (仅在ON时发送)
 *
 * 注意：关闭时不能发送brightness，否则HA会认为灯应该亮，自动发送ON命令
 */
static void publish_light_state(const char *topic, const led_state_t *state)
{
    cJSON *json = cJSON_CreateObject();
    if (!json) return;

    cJSON_AddStringToObject(json, "state", state->state ? "ON" : "OFF");

    /* 只在开启时发送brightness和color */
    if (state->state) {
        cJSON_AddNumberToObject(json, "brightness", state->brightness);
        cJSON *color = cJSON_CreateObject();
        cJSON_AddNumberToObject(color, "r", state->red);
        cJSON_AddNumberToObject(color, "g", state->green);
        cJSON_AddNumberToObject(color, "b", state->blue);
        cJSON_AddItemToObject(json, "color", color);
    }

    char *str = cJSON_PrintUnformatted(json);
    if (str) {
        mqtt_ha_publish(topic, str, 1);
        free(str);
    }
    cJSON_Delete(json);
}

static void publish_all_states(void)
{
    device_caps_t caps = get_device_capabilities();

    if (caps.has_pwm_rgb) {
        for (int i = 0; i < caps.pwm_rgb_count && i < MAX_ENTITIES; i++) {
            led_state_t *state = led_pwm_rgb_get_state(i);
            if (state) publish_light_state(rgb_state_topic[i], state);
        }
    }
    if (caps.has_pwm_single) {
        for (int i = 0; i < caps.pwm_single_count && i < MAX_ENTITIES; i++) {
            led_state_t *state = led_pwm_single_get_state(i);
            if (state) publish_light_state(single_state_topic[i], state);
        }
    }
    if (caps.has_ws2812) {
        for (int i = 0; i < caps.ws2812_count && i < MAX_ENTITIES; i++) {
            led_state_t *state = led_ws2812_get_state(i);
            if (state) publish_light_state(ws2812_state_topic[i], state);
        }
    }
    if (caps.has_switch) {
        for (int i = 0; i < caps.switch_count && i < MAX_ENTITIES; i++) {
            mqtt_ha_publish(switch_state_topic[i], led_switch_get_state(i) ? "ON" : "OFF", 1);
        }
    }
}

/* ==========================================================================
 * 命令处理
 * ========================================================================== */

static void handle_light_command(int type, int index, const char *data, int data_len)
{
    /* type: 0=rgb, 1=single, 2=ws2812 */
    led_state_t *current = NULL;
    led_state_t new_state = {0};

    switch (type) {
        case 0: current = led_pwm_rgb_get_state(index); break;
        case 1: current = led_pwm_single_get_state(index); break;
        case 2: current = led_ws2812_get_state(index); break;
    }

    if (current) new_state = *current;

    char *json_str = malloc(data_len + 1);
    if (!json_str) return;
    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0';
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (root) {
        cJSON *state_item = cJSON_GetObjectItem(root, "state");
        if (state_item && cJSON_IsString(state_item)) {
            new_state.state = (strcmp(state_item->valuestring, "ON") == 0);
        }

        cJSON *brightness = cJSON_GetObjectItem(root, "brightness");
        if (brightness && cJSON_IsNumber(brightness)) {
            new_state.brightness = (uint8_t)brightness->valueint;
        }

        cJSON *color = cJSON_GetObjectItem(root, "color");
        if (color) {
            cJSON *r = cJSON_GetObjectItem(color, "r");
            cJSON *g = cJSON_GetObjectItem(color, "g");
            cJSON *b = cJSON_GetObjectItem(color, "b");
            if (r && cJSON_IsNumber(r)) new_state.red = (uint8_t)r->valueint;
            if (g && cJSON_IsNumber(g)) new_state.green = (uint8_t)g->valueint;
            if (b && cJSON_IsNumber(b)) new_state.blue = (uint8_t)b->valueint;
        }

        cJSON_Delete(root);
    } else {
        /* 纯文本 */
        if (data_len == 2 && strncmp(data, "ON", 2) == 0) {
            new_state.state = true;
        } else if (data_len == 3 && strncmp(data, "OFF", 3) == 0) {
            new_state.state = false;
        }
    }

    /* 默认亮度 */
    if (new_state.state && new_state.brightness == 0) {
        new_state.brightness = 255;
    }

    /**
     * 修复bug：需要点两次才能关闭
     * 原因：HA发送OFF命令后，设备立即回传状态，但此时HA还没处理完
     *       导致HA状态不同步，需要再点一次
     * 修复：加100ms延迟，确保HA处理完命令后再回传状态
     */
    vTaskDelay(pdMS_TO_TICKS(100));

    switch (type) {
        case 0:
            led_pwm_rgb_set_state(index, &new_state);
            publish_light_state(rgb_state_topic[index], &new_state);
            break;
        case 1:
            led_pwm_single_set_state(index, &new_state);
            publish_light_state(single_state_topic[index], &new_state);
            break;
        case 2:
            led_ws2812_set_state(index, &new_state);
            publish_light_state(ws2812_state_topic[index], &new_state);
            break;
    }
}

static void handle_switch_command(int index, const char *data, int data_len)
{
    bool new_state = false;

    char *json_str = malloc(data_len + 1);
    if (!json_str) return;
    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0';
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (root) {
        cJSON *state_item = cJSON_GetObjectItem(root, "state");
        if (state_item && cJSON_IsString(state_item)) {
            new_state = (strcmp(state_item->valuestring, "ON") == 0);
        }
        cJSON_Delete(root);
    } else {
        if (data_len == 2 && strncmp(data, "ON", 2) == 0) new_state = true;
        else if (data_len == 3 && strncmp(data, "OFF", 3) == 0) new_state = false;
        else return;
    }

    led_switch_set_state(index, new_state);
    mqtt_ha_publish(switch_state_topic[index], new_state ? "ON" : "OFF", 1);
    ESP_LOGI(TAG, "Switch %d -> %s", index, new_state ? "ON" : "OFF");
}

static void handle_device_command(const char *data, int data_len)
{
    char *json_str = malloc(data_len + 1);
    if (!json_str) return;
    memcpy(json_str, data, data_len);
    json_str[data_len] = '\0';
    cJSON *root = cJSON_Parse(json_str);
    free(json_str);

    if (!root) return;

    cJSON *action = cJSON_GetObjectItem(root, "action");
    if (!action || !cJSON_IsString(action)) {
        cJSON_Delete(root);
        return;
    }

    const char *action_str = action->valuestring;
    cJSON *request_id = cJSON_GetObjectItem(root, "request_id");

    if (strcmp(action_str, "reboot") == 0) {
        ESP_LOGI(TAG, "Reboot command received");
        cJSON *response = cJSON_CreateObject();
        if (request_id) cJSON_AddStringToObject(response, "request_id", request_id->valuestring);
        cJSON_AddBoolToObject(response, "success", true);
        char *response_str = cJSON_PrintUnformatted(response);
        if (response_str) {
            mqtt_ha_publish(device_response_topic, response_str, 0);
            free(response_str);
        }
        cJSON_Delete(response);
        cJSON_Delete(root);
        vTaskDelay(pdMS_TO_TICKS(1000));
        esp_restart();
    }

    cJSON_Delete(root);
}

/* ==========================================================================
 * MQTT事件处理
 * ========================================================================== */

static esp_err_t mqtt_event_handler(esp_mqtt_event_handle_t event)
{
    switch (event->event_id) {
        case MQTT_EVENT_CONNECTED:
            ESP_LOGI(TAG, "MQTT connected");

            mqtt_ha_publish(device_avail_topic, "online", 1);

            /* 订阅所有命令主题 */
            device_caps_t caps = get_device_capabilities();

            if (caps.has_pwm_rgb) {
                for (int i = 0; i < caps.pwm_rgb_count && i < MAX_ENTITIES; i++) {
                    esp_mqtt_client_subscribe(client, rgb_command_topic[i], 1);
                }
            }
            if (caps.has_pwm_single) {
                for (int i = 0; i < caps.pwm_single_count && i < MAX_ENTITIES; i++) {
                    esp_mqtt_client_subscribe(client, single_command_topic[i], 1);
                }
            }
            if (caps.has_ws2812) {
                for (int i = 0; i < caps.ws2812_count && i < MAX_ENTITIES; i++) {
                    esp_mqtt_client_subscribe(client, ws2812_command_topic[i], 1);
                }
            }
            if (caps.has_switch) {
                for (int i = 0; i < caps.switch_count && i < MAX_ENTITIES; i++) {
                    esp_mqtt_client_subscribe(client, switch_command_topic[i], 1);
                }
            }

            esp_mqtt_client_subscribe(client, device_command_topic, 1);
            ota_upgrade_subscribe();

            mqtt_ha_report_device_status();
            mqtt_ha_publish_discovery();
            publish_all_states();

            break;

        case MQTT_EVENT_DATA:
            /* 匹配PWM RGB命令 */
            for (int i = 0; i < MAX_ENTITIES; i++) {
                if (event->topic_len == strlen(rgb_command_topic[i]) &&
                    strncmp(event->topic, rgb_command_topic[i], event->topic_len) == 0) {
                    handle_light_command(0, i, event->data, event->data_len);
                    return ESP_OK;
                }
            }
            /* 匹配单色灯命令 */
            for (int i = 0; i < MAX_ENTITIES; i++) {
                if (event->topic_len == strlen(single_command_topic[i]) &&
                    strncmp(event->topic, single_command_topic[i], event->topic_len) == 0) {
                    handle_light_command(1, i, event->data, event->data_len);
                    return ESP_OK;
                }
            }
            /* 匹配WS2812命令 */
            for (int i = 0; i < MAX_ENTITIES; i++) {
                if (event->topic_len == strlen(ws2812_command_topic[i]) &&
                    strncmp(event->topic, ws2812_command_topic[i], event->topic_len) == 0) {
                    handle_light_command(2, i, event->data, event->data_len);
                    return ESP_OK;
                }
            }
            /* 匹配开关命令 */
            for (int i = 0; i < MAX_ENTITIES; i++) {
                if (event->topic_len == strlen(switch_command_topic[i]) &&
                    strncmp(event->topic, switch_command_topic[i], event->topic_len) == 0) {
                    handle_switch_command(i, event->data, event->data_len);
                    return ESP_OK;
                }
            }
            /* 匹配设备命令 */
            if (event->topic_len == strlen(device_command_topic) &&
                strncmp(event->topic, device_command_topic, event->topic_len) == 0) {
                handle_device_command(event->data, event->data_len);
                return ESP_OK;
            }
            break;

        case MQTT_EVENT_DISCONNECTED:
            ESP_LOGW(TAG, "MQTT disconnected");
            break;

        default:
            break;
    }
    return ESP_OK;
}

/* ==========================================================================
 * 公共API
 * ========================================================================== */

void mqtt_ha_init(void)
{
    ESP_LOGI(TAG, "MQTT init, broker: %s", CONFIG_MQTT_BROKER_URL);

    generate_topics();

    char client_id[32];
    snprintf(client_id, sizeof(client_id), "ESP8266_%s", base_device_id);

    esp_mqtt_client_config_t mqtt_cfg = {
        .event_handle = mqtt_event_handler,
        .uri = CONFIG_MQTT_BROKER_URL,
        .client_id = client_id,
        .lwt_topic = device_avail_topic,
        .lwt_msg = "offline",
        .lwt_qos = 1,
        .lwt_retain = 1,
        .buffer_size = 2048,
    };

    client = esp_mqtt_client_init(&mqtt_cfg);
    esp_mqtt_client_start(client);
    ESP_LOGI(TAG, "MQTT client started");
}

void mqtt_ha_publish(const char *topic, const char *payload, int retain)
{
    if (client && topic && payload) {
        esp_mqtt_client_publish(client, topic, payload, 0, 1, retain);
    }
}

esp_mqtt_client_handle_t mqtt_ha_get_client(void)
{
    return client;
}

void mqtt_ha_report_device_status(void)
{
    cJSON *status = cJSON_CreateObject();
    if (!status) return;

    device_caps_t caps = get_device_capabilities();

    cJSON_AddStringToObject(status, "device_id", base_device_id);
    cJSON_AddStringToObject(status, "firmware_version", FIRMWARE_VERSION);
    cJSON_AddNumberToObject(status, "free_heap", esp_get_free_heap_size());

    /* 设备能力 */
    cJSON_AddBoolToObject(status, "has_rgb", caps.has_pwm_rgb);
    cJSON_AddBoolToObject(status, "has_single", caps.has_pwm_single);
    cJSON_AddBoolToObject(status, "has_ws2812", caps.has_ws2812);
    cJSON_AddBoolToObject(status, "has_switch", caps.has_switch);

    /* WiFi信息 */
    wifi_ap_record_t ap_info;
    if (esp_wifi_sta_get_ap_info(&ap_info) == ESP_OK) {
        cJSON_AddNumberToObject(status, "rssi", ap_info.rssi);
        cJSON_AddStringToObject(status, "ssid", (const char *)ap_info.ssid);
    }

    char *status_str = cJSON_PrintUnformatted(status);
    if (status_str) {
        mqtt_ha_publish(device_status_topic, status_str, 0);
        free(status_str);
    }
    cJSON_Delete(status);
}
