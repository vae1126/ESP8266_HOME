/**
 * @file    app_main.c
 * @brief   ESP8266 智能家居 MQTT 控制器
 *
 * 支持：PWM RGB灯、单色灯、WS2812灯带、开关/继电器
 */

#include <stdio.h>
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_system.h"
#include "esp_log.h"
#include "nvs_flash.h"
#include "device_id.h"
#include "led_control.h"
#include "device_config.h"
#include "wifi_manager.h"
#include "mqtt_ha.h"
#include "ota_upgrade.h"
#include "rssi_reporter.h"
#include "version.h"

static const char *TAG = "app_main";

void app_main(void)
{
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  ESP8266 Smart Home Controller");
    ESP_LOGI(TAG, "  Firmware: %s", FIRMWARE_VERSION);
    ESP_LOGI(TAG, "============================================");

    /* 1. 初始化NVS */
    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "NVS partition truncated, erasing...");
        nvs_flash_erase();
        nvs_flash_init();
    }

    /* 2. 初始化设备ID */
    device_id_init();

    /* 3. 初始化LED/开关控制 */
    led_control_init();

    /* 4. 初始化WiFi（阻塞） */
    wifi_manager_init();

    /* 5. 初始化MQTT */
    mqtt_ha_init();

    /* 6. 初始化OTA */
    ota_upgrade_init();

    /* 7. 启动RSSI上报 */
    rssi_reporter_start();

    /* 打印设备能力 */
    device_caps_t caps = get_device_capabilities();
    ESP_LOGI(TAG, "============================================");
    ESP_LOGI(TAG, "  System ready!");
    ESP_LOGI(TAG, "  Device: %s", base_device_name);
    ESP_LOGI(TAG, "  PWM RGB: %d", caps.pwm_rgb_count);
    ESP_LOGI(TAG, "  Single:  %d", caps.pwm_single_count);
    ESP_LOGI(TAG, "  WS2812:  %d", caps.ws2812_count);
    ESP_LOGI(TAG, "  Switch:  %d", caps.switch_count);
    ESP_LOGI(TAG, "============================================");
}
