#include "wifi_manager.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/task.h"
#include "freertos/timers.h"
#include "esp_wifi.h"
#include "esp_smartconfig.h"
#include "esp_log.h"
#include "driver/gpio.h"
#include "tcpip_adapter.h"
#include <string.h>
#include <stdint.h>

static const char *TAG = "wifi_manager";

static EventGroupHandle_t s_wifi_event_group = NULL;
static TimerHandle_t sc_timer = NULL;
static TimerHandle_t wifi_watchdog_timer = NULL;
static TaskHandle_t s_reconnect_task_handle = NULL;
static volatile bool sc_in_progress = false;
static wifi_state_change_cb_t s_state_change_cb = NULL;
static local_ctrl_cb_t s_local_ctrl_cb = NULL;

void wifi_manager_set_state_change_cb(wifi_state_change_cb_t cb)
{
    s_state_change_cb = cb;
}

void wifi_manager_set_local_ctrl_cb(local_ctrl_cb_t cb)
{
    s_local_ctrl_cb = cb;
}

EventGroupHandle_t wifi_manager_get_event_group(void)
{
    return s_wifi_event_group;
}

#define SC_TIMEOUT_SECONDS  120
#define BOOT_GPIO           0
#define BOOT_HOLD_SECONDS   3

static bool has_saved_credentials(void)
{
    wifi_config_t wifi_config = {0};
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) return false;
    return strlen((char *)wifi_config.sta.ssid) > 0;
}

static const char *get_saved_ssid(void)
{
    static wifi_config_t wifi_config;
    memset(&wifi_config, 0, sizeof(wifi_config));
    esp_err_t err = esp_wifi_get_config(WIFI_IF_STA, &wifi_config);
    if (err != ESP_OK) return "";
    return (const char *)wifi_config.sta.ssid;
}

static bool check_boot_button(void)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    ESP_ERROR_CHECK(gpio_config(&io_conf));

    vTaskDelay(pdMS_TO_TICKS(50));
    if (gpio_get_level(BOOT_GPIO) == 1) {
        return false;
    }

    int hold_count = 1;
    for (int i = 1; i < BOOT_HOLD_SECONDS * 10; i++) {
        vTaskDelay(pdMS_TO_TICKS(100));
        if (gpio_get_level(BOOT_GPIO) == 0) {
            hold_count++;
            if (hold_count >= BOOT_HOLD_SECONDS * 10) {
                ESP_LOGI(TAG, "BOOT button held for %d seconds", BOOT_HOLD_SECONDS);
                return true;
            }
        } else {
            return false;
        }
    }
    return false;
}

static void sc_timer_callback(TimerHandle_t xTimer)
{
    ESP_LOGE(TAG, "SmartConfig timeout, restarting...");
    esp_restart();
}

static void smartconfig_task(void *pvParameters)
{
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group, BIT2,
                                           pdTRUE, pdFALSE, portMAX_DELAY);
    if (bits & BIT2) {
        if (sc_timer) xTimerStop(sc_timer, portMAX_DELAY);
        ESP_LOGI(TAG, "SmartConfig succeeded");
    }
    vTaskDelete(NULL);
}

#define WIFI_MAX_BACKOFF_MS  10000
#define WIFI_BASE_BACKOFF_MS 1000
#define WIFI_MAX_RETRIES     300
#define WIFI_WATCHDOG_SECONDS 180
#define WIFI_CONNECT_TIMEOUT_SECONDS 60

static void wifi_watchdog_callback(TimerHandle_t xTimer)
{
    ESP_LOGE(TAG, "Wi-Fi disconnected for %d seconds, restarting...", WIFI_WATCHDOG_SECONDS);
    esp_restart();
}

static void reconnect_task(void *pvParameters)
{
    int backoff_ms = (int)(intptr_t)pvParameters;

    vTaskDelay(pdMS_TO_TICKS(backoff_ms));

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "esp_wifi_connect failed: %s", esp_err_to_name(err));
    }

    s_reconnect_task_handle = NULL;
    vTaskDelete(NULL);
}

static void wifi_event_handler(void *arg, esp_event_base_t event_base,
                               int32_t event_id, void *event_data)
{
    static int retry_count = 0;
    static int backoff_ms = WIFI_BASE_BACKOFF_MS;

    if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_START) {
        esp_wifi_connect();
    }

    else if (event_base == WIFI_EVENT && event_id == WIFI_EVENT_STA_DISCONNECTED) {
        if (sc_in_progress) {
            ESP_LOGI(TAG, "SmartConfig active, skipping reconnect");
        } else {
            retry_count++;
            ESP_LOGI(TAG, "Wi-Fi disconnected, retry %d (backoff %dms)", retry_count, backoff_ms);

            if (s_reconnect_task_handle == NULL) {
                BaseType_t ret = xTaskCreate(reconnect_task, "wifi_reconnect", 2048,
                                             (void *)(intptr_t)backoff_ms, 5, &s_reconnect_task_handle);
                if (ret != pdPASS) {
                    ESP_LOGW(TAG, "Failed to create reconnect task, falling back to direct connect");
                    esp_wifi_connect();
                    s_reconnect_task_handle = NULL;
                }
            } else {
                ESP_LOGW(TAG, "Reconnect task already running, skipping");
            }

            backoff_ms = (backoff_ms < WIFI_MAX_BACKOFF_MS / 2)
                         ? backoff_ms * 2 : WIFI_MAX_BACKOFF_MS;
        }
    }

    else if (event_base == IP_EVENT && event_id == IP_EVENT_STA_GOT_IP) {
        ip_event_got_ip_t *event = (ip_event_got_ip_t *)event_data;
        ESP_LOGI(TAG, "Got IP: " IPSTR, IP2STR(&event->ip_info.ip));

        retry_count = 0;
        backoff_ms = WIFI_BASE_BACKOFF_MS;

        sc_in_progress = false;
        if (sc_timer) xTimerStop(sc_timer, portMAX_DELAY);
        if (wifi_watchdog_timer) xTimerStop(wifi_watchdog_timer, portMAX_DELAY);
        esp_smartconfig_stop();
        if (s_state_change_cb) s_state_change_cb(WIFI_STATE_CONNECTED);
        xEventGroupSetBits(s_wifi_event_group, WIFI_CONNECTED_BIT);
    }

    else if (event_base == SC_EVENT && event_id == SC_EVENT_GOT_SSID_PSWD) {
        ESP_LOGI(TAG, "SmartConfig got credentials");
        smartconfig_event_got_ssid_pswd_t *evt = (smartconfig_event_got_ssid_pswd_t *)event_data;

        char ssid[33] = {0};
        char password[65] = {0};
        strncpy(ssid, (char *)evt->ssid, sizeof(ssid) - 1);
        strncpy(password, (char *)evt->password, sizeof(password) - 1);
        ESP_LOGI(TAG, "New SSID: %s", ssid);

        wifi_config_t wifi_config = {0};
        strncpy((char *)wifi_config.sta.ssid, ssid, sizeof(wifi_config.sta.ssid) - 1);
        strncpy((char *)wifi_config.sta.password, password, sizeof(wifi_config.sta.password) - 1);
        wifi_config.sta.threshold.authmode = WIFI_AUTH_OPEN;
        ESP_ERROR_CHECK(esp_wifi_set_config(WIFI_IF_STA, &wifi_config));
        ESP_ERROR_CHECK(esp_wifi_connect());

        xEventGroupSetBits(s_wifi_event_group, BIT2);
    }
}

static void boot_button_task(void *pvParameters);

void wifi_manager_init(void)
{
    s_wifi_event_group = xEventGroupCreate();
    if (s_wifi_event_group == NULL) {
        ESP_LOGE(TAG, "Failed to create event group");
        ESP_ERROR_CHECK(ESP_ERR_NO_MEM);
    }

    tcpip_adapter_init();
    ESP_ERROR_CHECK(esp_event_loop_create_default());

    wifi_init_config_t cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_ERROR_CHECK(esp_wifi_init(&cfg));

    ESP_ERROR_CHECK(esp_event_handler_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                &wifi_event_handler, NULL));
    ESP_ERROR_CHECK(esp_event_handler_register(SC_EVENT, ESP_EVENT_ANY_ID,
                                                &wifi_event_handler, NULL));

    xTaskCreate(boot_button_task, "boot_btn_task", 4096, NULL, 5, NULL);

    sc_timer = xTimerCreate("sc_timer", pdMS_TO_TICKS(SC_TIMEOUT_SECONDS * 1000),
                            pdFALSE, NULL, sc_timer_callback);

    wifi_watchdog_timer = xTimerCreate("wifi_wdg", pdMS_TO_TICKS(WIFI_WATCHDOG_SECONDS * 1000),
                                       pdFALSE, NULL, wifi_watchdog_callback);

    bool boot_pressed = check_boot_button();
    bool has_creds = has_saved_credentials();

    if (boot_pressed) {
        ESP_LOGI(TAG, "BOOT button pressed, starting SmartConfig");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());

        sc_in_progress = true;
        if (s_state_change_cb) s_state_change_cb(WIFI_STATE_SMARTCONFIG);
        smartconfig_start_config_t sc_cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
        ESP_ERROR_CHECK(esp_smartconfig_start(&sc_cfg));

        if (sc_timer) xTimerStart(sc_timer, portMAX_DELAY);
        xTaskCreate(smartconfig_task, "sc_task", 4096, NULL, 5, NULL);
    } else if (has_creds) {
        ESP_LOGI(TAG, "Connecting with saved SSID: %s", get_saved_ssid());
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    } else {
        ESP_LOGW(TAG, "No Wi-Fi credentials saved");
        ESP_LOGW(TAG, "Long press BOOT button (GPIO 0) for 3 seconds to enter SmartConfig");
        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));
        ESP_ERROR_CHECK(esp_wifi_start());
    }

    ESP_LOGI(TAG, "Waiting for Wi-Fi connection (timeout %ds)...", WIFI_CONNECT_TIMEOUT_SECONDS);
    EventBits_t bits = xEventGroupWaitBits(s_wifi_event_group,
                                           WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_SECONDS * 1000));
    if (bits & WIFI_CONNECTED_BIT) {
        ESP_LOGI(TAG, "Wi-Fi connected successfully");
    } else {
        ESP_LOGW(TAG, "Wi-Fi connection timeout, restarting to retry...");
        esp_restart();
    }
}

static void boot_button_task(void *pvParameters)
{
    gpio_config_t io_conf = {
        .pin_bit_mask = (1ULL << BOOT_GPIO),
        .mode = GPIO_MODE_INPUT,
        .pull_up_en = GPIO_PULLUP_ENABLE,
        .pull_down_en = GPIO_PULLDOWN_DISABLE,
        .intr_type = GPIO_INTR_DISABLE
    };
    gpio_config(&io_conf);

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(100));

        if (gpio_get_level(BOOT_GPIO) != 0) continue;

        int hold_count = 1;
        for (int i = 1; i < BOOT_HOLD_SECONDS * 10; i++) {
            vTaskDelay(pdMS_TO_TICKS(100));
            if (gpio_get_level(BOOT_GPIO) == 0) {
                hold_count++;
                if (hold_count >= BOOT_HOLD_SECONDS * 10) {
                    ESP_LOGI(TAG, "BOOT button held for %d seconds, starting SmartConfig", BOOT_HOLD_SECONDS);

                        if (sc_in_progress) {
                            ESP_LOGW(TAG, "SmartConfig already in progress, ignoring button trigger");
                            break;
                        }
                        sc_in_progress = true;

                        esp_wifi_disconnect();
                        ESP_ERROR_CHECK(esp_wifi_set_mode(WIFI_MODE_STA));

                        if (s_state_change_cb) s_state_change_cb(WIFI_STATE_SMARTCONFIG);
                        smartconfig_start_config_t sc_cfg = SMARTCONFIG_START_CONFIG_DEFAULT();
                        ESP_ERROR_CHECK(esp_smartconfig_start(&sc_cfg));

                        if (sc_timer) xTimerStart(sc_timer, portMAX_DELAY);

                        EventBits_t sc_bits = xEventGroupWaitBits(s_wifi_event_group,
                                                                   BIT2,
                                                                   pdTRUE, pdFALSE,
                                                                   pdMS_TO_TICKS((SC_TIMEOUT_SECONDS + 10) * 1000));
                        if (sc_bits & BIT2) {
                            ESP_LOGI(TAG, "SmartConfig completed");
                        } else {
                            ESP_LOGW(TAG, "SmartConfig timed out");
                        }
                        sc_in_progress = false;

                        break;
                    }
                } else {
                    if (hold_count > 0 && hold_count < 5 && s_local_ctrl_cb) {
                        s_local_ctrl_cb();
                    }
                    break;
                }
            }
    }
}
