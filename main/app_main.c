#include "device_id.h"
#include "wifi_manager.h"
#include "switch_control.h"
#include "status_led.h"
#include "mqtt_ha.h"
#include "esp_log.h"
#include "nvs_flash.h"

static const char *TAG = "main";

static void on_wifi_state_change(wifi_manager_state_t state)
{
    switch (state) {
    case WIFI_STATE_SMARTCONFIG:
        status_led_set_state(STATUS_LED_SMARTCONFIG);
        break;
    case WIFI_STATE_CONNECTED:
        status_led_set_state(STATUS_LED_MQTT_CONNECTING);
        break;
    case WIFI_STATE_CONNECTING:
    default:
        break;
    }
}

void app_main(void)
{
    ESP_LOGI(TAG, "Starting application...");

    esp_err_t ret = nvs_flash_init();
    if (ret == ESP_ERR_NVS_NO_FREE_PAGES || ret == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        ret = nvs_flash_init();
    }
    ESP_ERROR_CHECK(ret);

    device_id_init();
    switch_control_init();

    status_led_init();
    status_led_set_state(STATUS_LED_STARTUP);

    wifi_manager_set_state_change_cb(on_wifi_state_change);
    status_led_set_state(STATUS_LED_WIFI_CONNECTING);
    wifi_manager_init();

    status_led_set_state(STATUS_LED_MQTT_CONNECTING);
    mqtt_ha_init();

    status_led_set_state(STATUS_LED_READY);

    ESP_LOGI(TAG, "System ready");
    ESP_LOGI(TAG, "Firmware version: %s", FIRMWARE_VERSION);
}
