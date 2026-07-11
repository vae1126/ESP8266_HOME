/**
 * @file    wifi_manager.c
 * @brief   Wi-Fi管理实现 (ESP8266版本)
 */

#include <string.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/event_groups.h"
#include "esp_system.h"
#include "esp_wifi.h"
#include "esp_event_loop.h"
#include "esp_log.h"
#include "esp_smartconfig.h"
#include "nvs_flash.h"
#include "nvs.h"
#include "tcpip_adapter.h"
#include "wifi_manager.h"

static const char *TAG = "wifi_mgr";

/* NVS命名空间 */
#define NVS_NAMESPACE "wifi_cred"

/* SmartConfig超时 */
#define SC_TIMEOUT_SECONDS 60

/* WiFi重试次数后触发SmartConfig */
#define WIFI_RETRY_BEFORE_SC 5

/* 事件组 */
static EventGroupHandle_t s_wifi_event_group;

/* 状态变量 */
static int s_retry_num = 0;
static bool s_smartconfig_active = false;

/**
 * @brief  保存WiFi凭据到NVS
 */
static void save_wifi_credentials(const char *ssid, const char *password)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "NVS open failed: %s", esp_err_to_name(err));
        return;
    }

    nvs_set_str(handle, "ssid", ssid);
    nvs_set_str(handle, "password", password);
    nvs_commit(handle);
    nvs_close(handle);

    ESP_LOGI(TAG, "WiFi credentials saved");
}

/**
 * @brief  从NVS加载WiFi凭据
 */
static bool load_wifi_credentials(char *ssid, size_t ssid_len, char *password, size_t pass_len)
{
    nvs_handle_t handle;
    esp_err_t err = nvs_open(NVS_NAMESPACE, NVS_READONLY, &handle);
    if (err != ESP_OK) {
        return false;
    }

    /* nvs_get_str 需要指针传递长度，且会修改为实际长度 */
    size_t len = ssid_len;
    err = nvs_get_str(handle, "ssid", ssid, &len);
    if (err != ESP_OK) {
        nvs_close(handle);
        return false;
    }

    len = pass_len;
    err = nvs_get_str(handle, "password", password, &len);
    nvs_close(handle);

    return (err == ESP_OK);
}

/**
 * @brief  SmartConfig任务
 */
static void smartconfig_task(void *pvParameters)
{
    ESP_LOGI(TAG, "SmartConfig task started");

    while (1) {
        EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT,
                                               pdTRUE, pdFALSE, portMAX_DELAY);

        if (bits & WIFI_CONNECTED_BIT) {
            ESP_LOGI(TAG, "SmartConfig succeeded");
            break;
        }
    }

    esp_smartconfig_stop();
    s_smartconfig_active = false;
    vTaskDelete(NULL);
}

/**
 * @brief  WiFi事件处理函数 (ESP8266版本)
 */
static esp_err_t wifi_event_handler(void *ctx, system_event_t *event)
{
    switch (event->event_id) {
        case SYSTEM_EVENT_STA_START:
            ESP_LOGI(TAG, "WiFi STA started");
            if (!s_smartconfig_active) {
                esp_wifi_connect();
            }
            break;

        case SYSTEM_EVENT_STA_DISCONNECTED:
            ESP_LOGW(TAG, "WiFi disconnected");
            if (s_retry_num < WIFI_RETRY_BEFORE_SC) {
                esp_wifi_connect();
                s_retry_num++;
                ESP_LOGI(TAG, "Retrying WiFi (%d/%d)", s_retry_num, WIFI_RETRY_BEFORE_SC);
            } else {
                ESP_LOGW(TAG, "Too many retries, starting SmartConfig");
                s_retry_num = 0;
                if (!s_smartconfig_active) {
                    s_smartconfig_active = true;
                    esp_smartconfig_set_type(SC_TYPE_ESPTOUCH);
                    smartconfig_start_config_t sc_cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
                    esp_smartconfig_start(&sc_cfg);
                    xTaskCreate(smartconfig_task, "smartconfig", 4096, NULL, 5, NULL);
                }
            }
            break;

        case SYSTEM_EVENT_STA_GOT_IP:
            ESP_LOGI(TAG, "Got IP: %s", ip4addr_ntoa(&event->event_info.got_ip.ip_info.ip));
            s_retry_num = 0;
            xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
            break;

        case SYSTEM_EVENT_STA_CONNECTED:
            ESP_LOGI(TAG, "WiFi connected");
            break;

        default:
            break;
    }

    return ESP_OK;
}

/**
 * @brief  SmartConfig事件处理
 */
static void sc_event_handler(void *arg, esp_event_base_t event_base,
                             int32_t event_id, void *event_data)
{
    if (event_base == SC_EVENT && event_id == SC_EVENT_SCAN_DONE) {
        ESP_LOGI(TAG, "SmartConfig scan done");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_FOUND_CHANNEL) {
        ESP_LOGI(TAG, "SmartConfig found channel");
    } else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;

        ESP_LOGI(TAG, "SmartConfig got SSID: %s", (char *)evt->ssid);

        /* 保存凭据 */
        save_wifi_credentials((const char *)evt->ssid, (const char *)evt->password);

        /* 连接WiFi */
        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config));
        memcpy(wifi_config.sta.ssid, evt->ssid, sizeof(wifi_config.sta.ssid));
        memcpy(wifi_config.sta.password, evt->password, sizeof(wifi_config.sta.password));

        esp_wifi_disconnect();
        esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
        esp_wifi_connect();
    }
}

void wifi_manager_init(void)
{
    ESP_LOGI(TAG, "WiFi manager init");

    /* 创建事件组 */
    s_wifi_event_group = xEventGroupCreate();

    /* 初始化TCP/IP适配器 */
    tcpip_adapter_init();

    /* 注册WiFi事件处理 */
    esp_event_loop_init(wifi_event_handler, NULL);

    /* 注册SmartConfig事件处理 */
    esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID, &sc_event_handler, NULL);

    /* 初始化WiFi */
    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    esp_wifi_init(&cfg);
    esp_wifi_set_mode(WIFI_MODE_STA);

    /* 尝试加载保存的凭据 */
    char ssid[33] = {0};
    char password[65] = {0};
    bool has_creds = load_wifi_credentials(ssid, sizeof(ssid), password, sizeof(password));

    if (!has_creds) {
        ESP_LOGI(TAG, "No saved credentials, starting SmartConfig");
        s_smartconfig_active = true;

        esp_wifi_start();

        esp_smartconfig_set_type(SC_TYPE_ESPTOUCH);
        smartconfig_start_config_t sc_cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
        esp_smartconfig_start(&sc_cfg);

        xTaskCreate(smartconfig_task, "smartconfig", 4096, NULL, 5, NULL);
    } else {
        ESP_LOGI(TAG, "Connecting to saved WiFi: %s", ssid);

        wifi_config_t wifi_config;
        memset(&wifi_config, 0, sizeof(wifi_config));
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);

        esp_wifi_set_config(ESP_IF_WIFI_STA, &wifi_config);
        esp_wifi_start();
        esp_wifi_connect();
    }

    /* 等待连接成功 */
    xEventGroupWaitBits(s_wifi_event_group, WIFI_CONNECTED_BIT, pdFALSE, pdFALSE, portMAX_DELAY);

    ESP_LOGI(TAG, "WiFi connected");
}

EventGroupHandle_t wifi_manager_get_event_group(void)
{
    return s_wifi_event_group;
}
